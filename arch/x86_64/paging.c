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
#define PTE_ADDRESS UINT64_C(0x000ffffffffff000)

static phys_addr_t root_physical;
static uint64_t physical_limit;
static uint64_t bootstrap_offset;
static bool active;
static bool scratch_busy[2];

static bool canonical(uintptr_t address)
{
  return address <= UINT64_C(0x00007fffffffffff) ||
         address >= UINT64_C(0xffff800000000000);
}

static bool physical_valid(phys_addr_t physical)
{
  return !(physical & (PAGE_SIZE - 1)) && physical < physical_limit;
}

static unsigned index_at(uintptr_t address, unsigned level)
{
  return (address >> (12 + (level - 1) * 9)) & 511;
}

static uintptr_t recursive_address(unsigned a, unsigned b, unsigned c, unsigned d)
{
  return UINT64_C(0xffff000000000000) | ((uint64_t)a << 39) |
         ((uint64_t)b << 30) | ((uint64_t)c << 21) | ((uint64_t)d << 12);
}

static volatile uint64_t *active_table(uintptr_t address, unsigned level)
{
  const unsigned r = RECURSIVE_SLOT;
  unsigned a = index_at(address, 4), b = index_at(address, 3), c = index_at(address, 2);
  switch (level) {
  case 4: return (void *)recursive_address(r, r, r, r);
  case 3: return (void *)recursive_address(r, r, r, a);
  case 2: return (void *)recursive_address(r, r, a, b);
  case 1: return (void *)recursive_address(r, a, b, c);
  default: panic("invalid paging level");
  }
}

static uint64_t leaf_flags(unsigned permissions)
{
  return PTE_PRESENT | ((permissions & PAGE_WRITE) ? PTE_WRITE : 0) |
         ((permissions & PAGE_EXEC) ? 0 : PTE_NX);
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
  for (unsigned level = 4; level > 1; --level) {
    uint64_t *entry = &table[index_at(address, level)];
    if (!(*entry & PTE_PRESENT)) {
      phys_addr_t child = bootstrap_new_table();
      *entry = child | PTE_PRESENT | PTE_WRITE;
    }
    KASSERT(!(*entry & PTE_LARGE));
    table = bootstrap_pointer(*entry & PTE_ADDRESS, PAGE_SIZE);
  }
  return &table[index_at(address, 1)];
}

static void bootstrap_map(uintptr_t virtual, phys_addr_t physical, unsigned permissions)
{
  KASSERT(canonical(virtual) && virtual >= UINT64_C(0xffff800000000000));
  KASSERT(!(virtual & (PAGE_SIZE - 1)) && physical_valid(physical));
  KASSERT(permissions_valid(permissions));
  uint64_t *entry = bootstrap_leaf(virtual);
  KASSERT(!(*entry & PTE_PRESENT));
  *entry = physical | leaf_flags(permissions);
}

static void map_kernel_section(const struct boot_info *boot, char *start, char *end,
                               unsigned permissions)
{
  for (uintptr_t virtual = (uintptr_t)start; virtual < (uintptr_t)end; virtual += PAGE_SIZE) {
    bootstrap_map(virtual, boot->kernel_phys + (virtual - boot->kernel_virt), permissions);
  }
}

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
  __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static void configure_cpu(void)
{
  uint32_t a, b, c, d;
  cpuid(0x80000000, &a, &b, &c, &d);
  if (a < 0x80000008) {
    panic("CPU does not report physical address width");
  }
  cpuid(0x80000001, &a, &b, &c, &d);
  if (!(d & (1u << 20))) {
    panic("CPU lacks required NX support");
  }
  cpuid(0x80000008, &a, &b, &c, &d);
  unsigned physical_bits = a & 0xff;
  if (physical_bits < 32 || physical_bits > 52 || ((a >> 8) & 0xff) < 48) {
    panic("unsupported CPU address widths");
  }
  physical_limit = UINT64_C(1) << physical_bits;
  uint64_t cr0, cr4;
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
  if (cr4 & (UINT64_C(1) << 12)) {
    panic("five-level paging unexpectedly active");
  }
  /* No PCID or global translations survive the root replacement. */
  cr4 &= ~((UINT64_C(1) << 17) | (UINT64_C(1) << 7));
  __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
  __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080));
  a |= 1u << 11;
  __asm__ volatile("wrmsr" : : "a"(a), "d"(d), "c"(0xc0000080) : "memory");
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
  cr0 |= UINT64_C(1) << 16;
  __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
  klog("x86_64: physical address width=%u, NX and supervisor write protection enabled\n",
       physical_bits);
}

void paging_init(struct boot_info *boot)
{
  _Static_assert(ARCH_PAGE_SIZE == PAGE_SIZE, "page size interface");
  configure_cpu();
  bootstrap_offset = boot->bootstrap_direct_offset;
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *r = &boot->regions[i];
    if ((r->type == BOOT_USABLE || r->type == BOOT_KERNEL) &&
        (r->base >= physical_limit || r->length > physical_limit - r->base)) {
      panic("RAM outside CPU physical address width");
    }
  }
  struct pmm_bootstrap plan = pmm_plan(boot);
  size_t metadata_bytes = plan.metadata_pages * PAGE_SIZE;
  pmm_init(boot, plan, bootstrap_pointer(plan.metadata_phys, metadata_bytes));
  root_physical = bootstrap_new_table();
  map_kernel_section(boot, __text_start, __text_end, PAGE_EXEC);
  map_kernel_section(boot, __rodata_start, __rodata_end, 0);
  map_kernel_section(boot, __data_start, __data_end, PAGE_WRITE);
  for (size_t offset = 0; offset < metadata_bytes; offset += PAGE_SIZE) {
    bootstrap_map(PMM_METADATA_BASE + offset, plan.metadata_phys + offset, PAGE_WRITE);
  }
  /* Both scratch leaves start absent, but every ancestor is already allocated. */
  KASSERT(!*bootstrap_leaf(TEMP_MAP_BASE));
  KASSERT(!*bootstrap_leaf(TEMP_MAP_BASE + PAGE_SIZE));
  uint64_t *root = bootstrap_pointer(root_physical, PAGE_SIZE);
  KASSERT(!root[RECURSIVE_SLOT]);
  root[RECURSIVE_SLOT] = root_physical | PTE_PRESENT | PTE_WRITE | PTE_NX;
  klog("paging: switching CR3 from 0x%lx to owned root 0x%lx\n", read_cr3(), root_physical);
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
  KASSERT(active && physical_valid(physical) && slot < 2 && !scratch_busy[slot]);
  uintptr_t virtual = TEMP_MAP_BASE + slot * PAGE_SIZE;
  volatile uint64_t *entry = &active_table(virtual, 1)[index_at(virtual, 1)];
  KASSERT(!(*entry & PTE_PRESENT));
  scratch_busy[slot] = true;
  *entry = physical | PTE_PRESENT | PTE_WRITE | PTE_NX;
  invlpg(virtual);
  memset((void *)virtual, 0, PAGE_SIZE);
  __asm__ volatile("" : : : "memory");
  *entry = 0;
  invlpg(virtual);
  scratch_busy[slot] = false;
}

void arch_frame_zero(phys_addr_t physical)
{
  zero_via_scratch(physical, 1);
}

/* All ordinary ancestors are supervisor RW and executable, with permission
 * restrictions at the leaves. Still accumulate effective restrictions for query
 * and reject attempts to exceed any ancestor's permissions on mutation. */
static enum mm_result walk(uintptr_t virtual, bool create, volatile uint64_t **leaf,
                           unsigned *allowed)
{
  *allowed = PAGE_WRITE | PAGE_EXEC;
  for (unsigned level = 4; level > 1; --level) {
    volatile uint64_t *entry = &active_table(virtual, level)[index_at(virtual, level)];
    uint64_t value = *entry;
    if (!(value & PTE_PRESENT)) {
      if (!create) {
        return MM_NOT_MAPPED;
      }
      phys_addr_t child = pmm_alloc(1);
      if (!child) {
        return MM_NO_MEMORY;
      }
      zero_via_scratch(child, 0);
      value = child | PTE_PRESENT | PTE_WRITE;
      *entry = value;
      /* A parent change affects its recursive aliases as well as the target.
       * With no PCID/global pages, CR3 reload flushes all such cached walks. */
      write_cr3(root_physical);
    }
    if ((value & (PTE_LARGE | PTE_USER)) || !physical_valid(value & PTE_ADDRESS)) {
      panic("corrupt or unsupported page-table ancestor at %p", (void *)virtual);
    }
    if (!(value & PTE_WRITE)) {
      *allowed &= ~PAGE_WRITE;
    }
    if (value & PTE_NX) {
      *allowed &= ~PAGE_EXEC;
    }
  }
  *leaf = &active_table(virtual, 1)[index_at(virtual, 1)];
  return MM_OK;
}

enum mm_result arch_page_map(uintptr_t virtual, phys_addr_t physical, unsigned permissions)
{
  if (!mutable_address(virtual) || !physical_valid(physical) || !permissions_valid(permissions)) {
    return MM_INVALID;
  }
  volatile uint64_t *leaf;
  unsigned allowed;
  enum mm_result result = walk(virtual, true, &leaf, &allowed);
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
  enum mm_result result = walk(virtual, false, &leaf, &allowed);
  if (result != MM_OK) {
    return result;
  }
  if (!(*leaf & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  *physical = *leaf & PTE_ADDRESS;
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
  enum mm_result result = walk(virtual, false, &leaf, &allowed);
  if (result != MM_OK) {
    return result;
  }
  if (!(*leaf & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if (permissions & ~allowed) {
    return MM_INVALID;
  }
  *leaf = (*leaf & PTE_ADDRESS) | leaf_flags(permissions);
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
  enum mm_result status = walk(virtual, false, &leaf, &allowed);
  if (status != MM_OK) {
    return status;
  }
  uint64_t value = *leaf;
  if (!(value & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if ((value & PTE_USER) || !physical_valid(value & PTE_ADDRESS)) {
    panic("corrupt page-table leaf at %p", (void *)virtual);
  }
  if (!(value & PTE_WRITE)) {
    allowed &= ~PAGE_WRITE;
  }
  if (value & PTE_NX) {
    allowed &= ~PAGE_EXEC;
  }
  *result = (struct page_translation){
    .physical = (value & PTE_ADDRESS) | (virtual & (PAGE_SIZE - 1)),
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
