#include <arch/cpu.h>
#include <arch/layout.h>
#include <arch/paging.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>

#define PTE_PRESENT UINT64_C(1)
#define PTE_WRITE (UINT64_C(1) << 1)
#define PTE_USER (UINT64_C(1) << 2)
#define PTE_LARGE (UINT64_C(1) << 7)
#define PTE_NX (UINT64_C(1) << 63)

/* Only bits 51:12 belong to the frame address; never carry NX into an address. */
#define PTE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)

#define PAGE_OFFSET_BITS 12
#define TABLE_INDEX_BITS 9
#define TABLE_INDEX_MASK ((1u << TABLE_INDEX_BITS) - 1)
#define PT_INDEX_SHIFT PAGE_OFFSET_BITS
#define PD_INDEX_SHIFT (PT_INDEX_SHIFT + TABLE_INDEX_BITS)
#define PDPT_INDEX_SHIFT (PD_INDEX_SHIFT + TABLE_INDEX_BITS)
#define PML4_INDEX_SHIFT (PDPT_INDEX_SHIFT + TABLE_INDEX_BITS)
#define HIGHER_HALF_SIGN_EXTENSION (~((UINT64_C(1) << VIRTUAL_ADDRESS_BITS) - 1))

#define CPUID_EXTENDED_MAX 0x80000000
#define CPUID_EXTENDED_FEATURES 0x80000001
#define CPUID_ADDRESS_WIDTHS 0x80000008
#define CPUID_FEATURE_NX (1u << 20)
#define CPUID_ADDRESS_WIDTH_MASK 0xff
#define CPUID_VIRTUAL_WIDTH_SHIFT 8
#define MIN_PHYSICAL_ADDRESS_BITS 32
#define MAX_PHYSICAL_ADDRESS_BITS 52

#define CR0_WRITE_PROTECT (UINT64_C(1) << 16)
#define CR4_GLOBAL_PAGES (UINT64_C(1) << 7)
#define CR4_FIVE_LEVEL_PAGING (UINT64_C(1) << 12)
#define CR4_PCID_ENABLE (UINT64_C(1) << 17)
#define MSR_EFER 0xc0000080
#define EFER_NX_ENABLE (UINT64_C(1) << 11)

enum table_level {
  LEVEL_PT = 1,
  LEVEL_PD,
  LEVEL_PDPT,
  LEVEL_PML4,
};

enum scratch_slot {
  SCRATCH_TABLE,
  SCRATCH_DATA,
  SCRATCH_SLOT_COUNT,
};

static phys_addr_t root_physical;
static uint64_t physical_limit;
static uint64_t bootstrap_offset;
static bool active;
static bool scratch_busy[SCRATCH_SLOT_COUNT];

static bool canonical(uintptr_t address)
{
  return address <= LOWER_HALF_MAX || address >= HIGHER_HALF_BASE;
}

static bool physical_valid(phys_addr_t physical)
{
  return !(physical & (PAGE_SIZE - 1)) && physical < physical_limit;
}

static unsigned index_at(uintptr_t address, unsigned level)
{
  unsigned shift = PAGE_OFFSET_BITS + (level - LEVEL_PT) * TABLE_INDEX_BITS;
  return (address >> shift) & TABLE_INDEX_MASK;
}

static uintptr_t recursive_address(unsigned pml4, unsigned pdpt, unsigned pd,
                                   unsigned pt)
{
  return HIGHER_HALF_SIGN_EXTENSION | ((uint64_t)pml4 << PML4_INDEX_SHIFT) |
         ((uint64_t)pdpt << PDPT_INDEX_SHIFT) | ((uint64_t)pd << PD_INDEX_SHIFT) |
         ((uint64_t)pt << PT_INDEX_SHIFT);
}

static volatile uint64_t *active_table(uintptr_t address, unsigned level)
{
  const unsigned recursive = RECURSIVE_SLOT;
  unsigned pml4 = index_at(address, LEVEL_PML4);
  unsigned pdpt = index_at(address, LEVEL_PDPT);
  unsigned pd = index_at(address, LEVEL_PD);

  /* Each recursive index revisits the root instead of descending a level.
   * The remaining indices select the target table as a readable/writable page. */
  switch (level) {
  case LEVEL_PML4:
    return (void *)recursive_address(recursive, recursive, recursive, recursive);
  case LEVEL_PDPT:
    return (void *)recursive_address(recursive, recursive, recursive, pml4);
  case LEVEL_PD:
    return (void *)recursive_address(recursive, recursive, pml4, pdpt);
  case LEVEL_PT:
    return (void *)recursive_address(recursive, pml4, pdpt, pd);
  default:
    panic("invalid paging level");
  }
}

static uint64_t leaf_flags(unsigned permissions)
{
  uint64_t flags = PTE_PRESENT;
  if (permissions & PAGE_WRITE) {
    flags |= PTE_WRITE;
  }
  if (!(permissions & PAGE_EXEC)) {
    flags |= PTE_NX;
  }
  return flags;
}

static bool permissions_valid(unsigned permissions)
{
  return !(permissions & ~(PAGE_WRITE | PAGE_EXEC)) &&
         permissions != (PAGE_WRITE | PAGE_EXEC);
}

static bool mutable_address(uintptr_t address)
{
  return active && !(address & (PAGE_SIZE - 1)) && canonical(address) &&
         address >= KERNEL_VM_BASE && address - KERNEL_VM_BASE < KERNEL_VM_SIZE;
}

static void *bootstrap_pointer(phys_addr_t physical, size_t bytes)
{
  if (active || !bytes || physical >= physical_limit ||
      bytes > physical_limit - physical || physical > UINT64_MAX - bootstrap_offset ||
      bytes - 1 > UINT64_MAX - (bootstrap_offset + physical) ||
      !canonical(bootstrap_offset + physical + bytes - 1)) {
    panic("invalid bootstrap physical access 0x%lx", physical);
  }

  return (void *)(bootstrap_offset + physical);
}

static phys_addr_t bootstrap_new_table(void)
{
  phys_addr_t physical = pmm_alloc(1);
  if (!physical) {
    panic("no physical frame for bootstrap page table");
  }

  memset(bootstrap_pointer(physical, PAGE_SIZE), 0, PAGE_SIZE);
  return physical;
}

static uint64_t *bootstrap_leaf(uintptr_t address)
{
  uint64_t *table = bootstrap_pointer(root_physical, PAGE_SIZE);
  for (unsigned level = LEVEL_PML4; level > LEVEL_PT; --level) {
    uint64_t *entry = &table[index_at(address, level)];
    if (!(*entry & PTE_PRESENT)) {
      phys_addr_t child = bootstrap_new_table();
      *entry = child | PTE_PRESENT | PTE_WRITE;
    }

    KASSERT(!(*entry & PTE_LARGE));
    table = bootstrap_pointer(*entry & PTE_ADDRESS_MASK, PAGE_SIZE);
  }
  return &table[index_at(address, LEVEL_PT)];
}

static void bootstrap_map(uintptr_t virtual, phys_addr_t physical,
                          unsigned permissions)
{
  KASSERT(canonical(virtual) && virtual >= HIGHER_HALF_BASE);
  KASSERT(!(virtual & (PAGE_SIZE - 1)) && physical_valid(physical));
  KASSERT(permissions_valid(permissions));

  uint64_t *entry = bootstrap_leaf(virtual);
  KASSERT(!(*entry & PTE_PRESENT));
  *entry = physical | leaf_flags(permissions);
}

static void map_kernel_section(const struct boot_info *boot, char *start, char *end,
                               unsigned permissions)
{
  for (uintptr_t virtual = (uintptr_t)start; virtual < (uintptr_t)end;
       virtual += PAGE_SIZE) {
    phys_addr_t physical = boot->kernel_phys + (virtual - boot->kernel_virt);
    bootstrap_map(virtual, physical, permissions);
  }
}

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile("cpuid"
                   : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                   : "a"(leaf), "c"(0));
}

static unsigned detect_physical_address_width(void)
{
  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
  if (eax < CPUID_ADDRESS_WIDTHS) {
    panic("CPU does not report physical address width");
  }

  cpuid(CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_FEATURE_NX)) {
    panic("CPU lacks required NX support");
  }

  cpuid(CPUID_ADDRESS_WIDTHS, &eax, &ebx, &ecx, &edx);
  unsigned physical_bits = eax & CPUID_ADDRESS_WIDTH_MASK;
  unsigned virtual_bits =
    (eax >> CPUID_VIRTUAL_WIDTH_SHIFT) & CPUID_ADDRESS_WIDTH_MASK;
  if (physical_bits < MIN_PHYSICAL_ADDRESS_BITS ||
      physical_bits > MAX_PHYSICAL_ADDRESS_BITS ||
      virtual_bits < VIRTUAL_ADDRESS_BITS) {
    panic("unsupported CPU address widths");
  }
  return physical_bits;
}

static void configure_cpu(void)
{
  unsigned physical_bits = detect_physical_address_width();
  physical_limit = UINT64_C(1) << physical_bits;

  uint64_t cr4 = read_cr4();
  if (cr4 & CR4_FIVE_LEVEL_PAGING) {
    panic("five-level paging unexpectedly active");
  }

  /* No PCID or global translations survive the root replacement. */
  write_cr4(cr4 & ~(CR4_PCID_ENABLE | CR4_GLOBAL_PAGES));

  /* NX must be enabled before publishing NX entries; WP makes read-only
   * kernel pages read-only even to supervisor writes. */
  write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_NX_ENABLE);
  write_cr0(read_cr0() | CR0_WRITE_PROTECT);

  klog("x86_64: physical address width=%u, NX and supervisor write protection enabled\n",
       physical_bits);
}

static void validate_ram_address_width(const struct boot_info *boot)
{
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if ((region->type == BOOT_USABLE || region->type == BOOT_KERNEL) &&
        (region->base >= physical_limit ||
         region->length > physical_limit - region->base)) {
      panic("RAM outside CPU physical address width");
    }
  }
}

void paging_init(struct boot_info *boot)
{
  _Static_assert(ARCH_PAGE_SIZE == PAGE_SIZE, "page size interface");

  configure_cpu();
  bootstrap_offset = boot->bootstrap_direct_offset;
  validate_ram_address_width(boot);

  struct pmm_bootstrap plan = pmm_plan(boot);
  size_t metadata_bytes = plan.metadata_pages * PAGE_SIZE;
  pmm_init(boot, plan, bootstrap_pointer(plan.metadata_phys, metadata_bytes));

  root_physical = bootstrap_new_table();
  map_kernel_section(boot, __text_start, __text_end, PAGE_EXEC);
  map_kernel_section(boot, __rodata_start, __rodata_end, 0);
  map_kernel_section(boot, __data_start, __data_end, PAGE_WRITE);

  for (size_t offset = 0; offset < metadata_bytes; offset += PAGE_SIZE) {
    bootstrap_map(PMM_METADATA_BASE + offset, plan.metadata_phys + offset,
                  PAGE_WRITE);
  }

  /* Both scratch leaves start absent, but every ancestor is already allocated. */
  KASSERT(!*bootstrap_leaf(TEMP_MAP_BASE + SCRATCH_TABLE * PAGE_SIZE));
  KASSERT(!*bootstrap_leaf(TEMP_MAP_BASE + SCRATCH_DATA * PAGE_SIZE));

  uint64_t *root = bootstrap_pointer(root_physical, PAGE_SIZE);
  KASSERT(!root[RECURSIVE_SLOT]);
  root[RECURSIVE_SLOT] = root_physical | PTE_PRESENT | PTE_WRITE | PTE_NX;

  klog("paging: switching CR3 from 0x%lx to owned root 0x%lx\n",
       read_cr3(), root_physical);
  write_cr3(root_physical);
  active = true;
  pmm_rebase((void *)PMM_METADATA_BASE);
  bootstrap_offset = 0;
  boot->bootstrap_direct_offset = 0;

  klog("paging: owned CR3=0x%lx; 4 KiB leaves, recursive slot %u, no HHDM\n",
       read_cr3(), RECURSIVE_SLOT);
}

static void zero_via_scratch(phys_addr_t physical, unsigned slot)
{
  KASSERT(active && physical_valid(physical) && slot < SCRATCH_SLOT_COUNT &&
          !scratch_busy[slot]);

  uintptr_t virtual = TEMP_MAP_BASE + slot * PAGE_SIZE;
  volatile uint64_t *table = active_table(virtual, LEVEL_PT);
  volatile uint64_t *entry = &table[index_at(virtual, LEVEL_PT)];
  KASSERT(!(*entry & PTE_PRESENT));

  scratch_busy[slot] = true;
  *entry = physical | PTE_PRESENT | PTE_WRITE | PTE_NX;
  invlpg(virtual);

  memset((void *)virtual, 0, PAGE_SIZE);

  /* Keep the zeroing stores before withdrawal of the scratch mapping. */
  __asm__ volatile("" : : : "memory");
  *entry = 0;
  invlpg(virtual);
  scratch_busy[slot] = false;
}

void arch_frame_zero(phys_addr_t physical)
{
  zero_via_scratch(physical, SCRATCH_DATA);
}

/* All ordinary ancestors are supervisor RW and executable, with permission
 * restrictions at the leaves. Still accumulate effective restrictions for query
 * and reject attempts to exceed any ancestor's permissions on mutation. */
static enum mm_result walk_to_leaf(uintptr_t virtual, bool create,
                                   volatile uint64_t **leaf, unsigned *allowed)
{
  *allowed = PAGE_WRITE | PAGE_EXEC;
  for (unsigned level = LEVEL_PML4; level > LEVEL_PT; --level) {
    volatile uint64_t *table = active_table(virtual, level);
    volatile uint64_t *entry = &table[index_at(virtual, level)];
    uint64_t value = *entry;
    if (!(value & PTE_PRESENT)) {
      if (!create) {
        return MM_NOT_MAPPED;
      }

      phys_addr_t child = pmm_alloc(1);
      if (!child) {
        return MM_NO_MEMORY;
      }

      zero_via_scratch(child, SCRATCH_TABLE);
      value = child | PTE_PRESENT | PTE_WRITE;
      *entry = value;

      /* A parent change affects its recursive aliases as well as the target.
       * With no PCID/global pages, CR3 reload flushes all such cached walks. */
      write_cr3(root_physical);
    }

    if ((value & (PTE_LARGE | PTE_USER)) ||
        !physical_valid(value & PTE_ADDRESS_MASK)) {
      panic("corrupt or unsupported page-table ancestor at %p", (void *)virtual);
    }

    if (!(value & PTE_WRITE)) {
      *allowed &= ~PAGE_WRITE;
    }
    if (value & PTE_NX) {
      *allowed &= ~PAGE_EXEC;
    }
  }

  *leaf = &active_table(virtual, LEVEL_PT)[index_at(virtual, LEVEL_PT)];
  return MM_OK;
}

enum mm_result arch_page_map(uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions)
{
  if (!mutable_address(virtual) || !physical_valid(physical) ||
      !permissions_valid(permissions)) {
    return MM_INVALID;
  }

  volatile uint64_t *leaf;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(virtual, true, &leaf, &allowed);
  if (result != MM_OK) {
    return result;
  }
  if (*leaf & PTE_PRESENT) {
    return MM_COLLISION;
  }
  if (permissions & ~allowed) {
    return MM_INVALID;
  }

  *leaf = physical | leaf_flags(permissions);
  invlpg(virtual);
  return MM_OK;
}

enum mm_result arch_page_unmap(uintptr_t virtual, phys_addr_t *physical)
{
  if (!mutable_address(virtual) || !physical) {
    return MM_INVALID;
  }

  volatile uint64_t *leaf;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(virtual, false, &leaf, &allowed);
  if (result != MM_OK) {
    return result;
  }
  if (!(*leaf & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }

  *physical = *leaf & PTE_ADDRESS_MASK;
  *leaf = 0;
  invlpg(virtual);
  return MM_OK;
}

enum mm_result arch_page_protect(uintptr_t virtual, unsigned permissions)
{
  if (!mutable_address(virtual) || !permissions_valid(permissions)) {
    return MM_INVALID;
  }

  volatile uint64_t *leaf;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(virtual, false, &leaf, &allowed);
  if (result != MM_OK) {
    return result;
  }
  if (!(*leaf & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if (permissions & ~allowed) {
    return MM_INVALID;
  }

  *leaf = (*leaf & PTE_ADDRESS_MASK) | leaf_flags(permissions);
  invlpg(virtual);
  return MM_OK;
}

enum mm_result arch_page_query(uintptr_t virtual, struct page_translation *result)
{
  if (!active || !canonical(virtual) || !result) {
    return MM_INVALID;
  }

  volatile uint64_t *leaf;
  unsigned allowed;
  enum mm_result status = walk_to_leaf(virtual, false, &leaf, &allowed);
  if (status != MM_OK) {
    return status;
  }

  uint64_t value = *leaf;
  if (!(value & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if ((value & PTE_USER) || !physical_valid(value & PTE_ADDRESS_MASK)) {
    panic("corrupt page-table leaf at %p", (void *)virtual);
  }

  if (!(value & PTE_WRITE)) {
    allowed &= ~PAGE_WRITE;
  }
  if (value & PTE_NX) {
    allowed &= ~PAGE_EXEC;
  }

  *result = (struct page_translation){
    .physical = (value & PTE_ADDRESS_MASK) | (virtual & (PAGE_SIZE - 1)),
    .permissions = allowed,
  };
  return MM_OK;
}

uintptr_t arch_vm_base(void)
{
  return KERNEL_VM_BASE;
}

size_t arch_vm_size(void)
{
  return KERNEL_VM_SIZE;
}
