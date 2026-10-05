#include <arch/acpi.h>
#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/io_apic.h>
#include <arch/clock.h>
#include <arch/layout.h>
#include <arch/paging.h>
#include <arch/pci.h>
#include <kernel/fb/early_console.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>

#define PTE_PRESENT UINT64_C(1)
#define PTE_WRITE (UINT64_C(1) << 1)
#define PTE_USER (UINT64_C(1) << 2)
#define PTE_WRITE_THROUGH (UINT64_C(1) << 3)
#define PTE_CACHE_DISABLE (UINT64_C(1) << 4)
#define PTE_LARGE (UINT64_C(1) << 7)
/* Bit 7 selects PAT in a 4 KiB leaf; in higher-level entries it means large. */
#define PTE_PAT_4K (UINT64_C(1) << 7)
#define PTE_NX (UINT64_C(1) << 63)

#define PAT_FRAMEBUFFER_INDEX 5
#define PAT_ENTRY_BITS 8
#define PAT_TYPE_MASK UINT64_C(0xff)
#define PAT_WRITE_COMBINING 1
#define PAT_DEVICE_INDEX 3
#define PAT_UNCACHEABLE 0

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
#define CPUID_ADDRESS_WIDTHS 0x80000008
#define CPUID_ADDRESS_WIDTH_MASK 0xff
#define CPUID_VIRTUAL_WIDTH_SHIFT 8
#define MIN_PHYSICAL_ADDRESS_BITS 32
#define MAX_PHYSICAL_ADDRESS_BITS 52

#define CR0_WRITE_PROTECT (UINT64_C(1) << 16)
#define CR4_GLOBAL_PAGES (UINT64_C(1) << 7)
#define CR4_FIVE_LEVEL_PAGING (UINT64_C(1) << 12)
#define CR4_PCID_ENABLE (UINT64_C(1) << 17)

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

static struct arch_address_space kernel_space;
static uint64_t physical_limit;
static uint64_t bootstrap_offset;
static bool active;

/* Each CPU index owns one pair of slots. Only that CPU reads or changes its row
 * or remaps its pages, so its local invlpg is sufficient. */
static bool scratch_busy[XAPIC_CPU_LIMIT][SCRATCH_SLOT_COUNT];
_Static_assert(XAPIC_CPU_LIMIT * SCRATCH_SLOT_COUNT * ARCH_PAGE_SIZE <=
               APIC_BASE - TEMP_MAP_BASE, "scratch slots for every CPU");

static uintptr_t scratch_address(size_t cpu, unsigned slot)
{
  return TEMP_MAP_BASE + (cpu * SCRATCH_SLOT_COUNT + slot) * PAGE_SIZE;
}

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
  if (permissions & PAGE_USER) {
    flags |= PTE_USER;
  }
  return flags;
}

static bool permissions_valid(unsigned permissions)
{
  return !(permissions & ~(PAGE_WRITE | PAGE_EXEC | PAGE_USER)) &&
         (permissions & (PAGE_WRITE | PAGE_EXEC)) != (PAGE_WRITE | PAGE_EXEC);
}

static bool space_valid(const struct arch_address_space *space)
{
  return active && space && space->root && physical_valid(space->root);
}

static bool mutable_address(const struct arch_address_space *space,
                            uintptr_t address)
{
  if (!space_valid(space) || (address & (PAGE_SIZE - 1))) {
    return false;
  }

  if (space == &kernel_space) {
    return address >= KERNEL_VM_BASE &&
           address - KERNEL_VM_BASE < KERNEL_VM_SIZE;
  }
  return address >= PAGE_SIZE && address <= LOWER_HALF_MAX;
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
  uint64_t *table = bootstrap_pointer(kernel_space.root, PAGE_SIZE);
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

static void configure_paging_cpu(void)
{
  uint64_t cr4 = read_cr4();
  if (cr4 & CR4_FIVE_LEVEL_PAGING) {
    panic("five-level paging unexpectedly active");
  }

  /* No PCID or global translations survive the root replacement. */
  write_cr4(cr4 & ~(CR4_PCID_ENABLE | CR4_GLOBAL_PAGES));

  /* NX must be enabled before publishing NX entries; WP makes read-only
   * kernel pages read-only even to supervisor writes. */
  write_msr(IA32_EFER, read_msr(IA32_EFER) | EFER_NXE);
  write_cr0(read_cr0() | CR0_WRITE_PROTECT);
}

static void configure_cpu(void)
{
  unsigned physical_bits = detect_physical_address_width();
  physical_limit = UINT64_C(1) << physical_bits;
  configure_paging_cpu();

  klog("x86_64: physical address width=%u, NX and supervisor write protection enabled\n",
       physical_bits);
}

void paging_prepare_ap(void)
{
  if ((UINT64_C(1) << detect_physical_address_width()) != physical_limit) {
    panic("AP physical address width differs from BSP");
  }
  configure_paging_cpu();
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

static void map_framebuffer(struct boot_framebuffer *fb)
{
  size_t page_offset = fb->physical & (PAGE_SIZE - 1);
  phys_addr_t first_frame = fb->physical - page_offset;
  if (!fb->size || fb->size > SIZE_MAX - page_offset - (PAGE_SIZE - 1)) {
    panic("framebuffer mapping extent overflows");
  }
  size_t bytes = (page_offset + fb->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (bytes > FRAMEBUFFER_END - FRAMEBUFFER_BASE || first_frame >= physical_limit ||
      bytes > physical_limit - first_frame) {
    panic("framebuffer outside supported address range");
  }

  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  if (!(edx & CPUID_FEATURE_PAT)) {
    panic("framebuffer mapping requires PAT support");
  }
  uint64_t pat = read_msr(IA32_PAT);
  if (((pat >> (PAT_FRAMEBUFFER_INDEX * PAT_ENTRY_BITS)) & PAT_TYPE_MASK) !=
      PAT_WRITE_COMBINING) {
    panic("unexpected framebuffer PAT memory type");
  }

  /* Limine specifies PAT[5]=WC. Preserve that memory type across the CR3
   * switch: 4 KiB leaves select index 5 with PAT=1, PCD=0, PWT=1. This avoids
   * treating device memory as ordinary write-back RAM. No HHDM alias remains. */
  for (size_t offset = 0; offset < bytes; offset += PAGE_SIZE) {
    uintptr_t address = FRAMEBUFFER_BASE + offset;
    bootstrap_map(address, first_frame + offset, PAGE_WRITE);
    *bootstrap_leaf(address) |= PTE_PAT_4K | PTE_WRITE_THROUGH;
  }
  fb->address = FRAMEBUFFER_BASE + page_offset;
}

static void map_local_apic(void)
{
  uint64_t pat = read_msr(IA32_PAT);
  if (((pat >> (PAT_DEVICE_INDEX * PAT_ENTRY_BITS)) & PAT_TYPE_MASK) != PAT_UNCACHEABLE) {
    panic("unexpected APIC PAT memory type");
  }

  bootstrap_map(APIC_BASE, apic_physical_address(), PAGE_WRITE);
  /* PCD=1, PWT=1 select Limine's PAT entry 3: device registers must be UC. */
  *bootstrap_leaf(APIC_BASE) |= PTE_CACHE_DISABLE | PTE_WRITE_THROUGH;

  phys_addr_t io_apic = io_apic_physical_address();
  if (io_apic) {
    if (!physical_valid(io_apic)) {
      panic("invalid I/O APIC physical address");
    }
    bootstrap_map(IO_APIC_BASE, io_apic, PAGE_WRITE);
    *bootstrap_leaf(IO_APIC_BASE) |= PTE_CACHE_DISABLE | PTE_WRITE_THROUGH;
  }
}

static void map_hpet(void)
{
  phys_addr_t physical = arch_clock_physical_address();
  if (!physical_valid(physical)) {
    panic("invalid HPET physical address");
  }
  bootstrap_map(HPET_BASE, physical, PAGE_WRITE);
  *bootstrap_leaf(HPET_BASE) |= PTE_CACHE_DISABLE | PTE_WRITE_THROUGH;
}

static unsigned map_pci_ecam(const struct boot_info *boot)
{
  struct pci_ecam ecam;
  if (!acpi_pci_ecam(boot, &ecam)) {
    return 0;
  }

  size_t bytes = ecam.bus_count * PCI_ECAM_BUS_BYTES;
  if (!physical_valid(ecam.physical) || bytes > PCI_ECAM_SIZE ||
      bytes > physical_limit - ecam.physical) {
    klog("PCI: MCFG aperture outside supported address range; discovery disabled\n");
    return 0;
  }
  /* MCFG is not a RAM reservation. Do not create an uncached alias of memory
   * owned by the allocator, firmware tables, boot payload or framebuffer. */
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->base < ecam.physical + bytes &&
        ecam.physical < region->base + region->length && region->type != BOOT_RESERVED) {
      klog("PCI: MCFG aperture overlaps non-reserved memory; discovery disabled\n");
      return 0;
    }
  }

  /* map_local_apic already checked PAT[3]=UC. Discovery has no config writes:
   * retain CR0.WP protection, NX, and the device cache type on every leaf. */
  for (size_t offset = 0; offset < bytes; offset += PAGE_SIZE) {
    uintptr_t address = PCI_ECAM_BASE + offset;
    bootstrap_map(address, ecam.physical + offset, 0);
    *bootstrap_leaf(address) |= PTE_CACHE_DISABLE | PTE_WRITE_THROUGH;
  }
  klog("PCI: ECAM physical=0x%lx bytes=0x%zx segment=0 buses=0..%u\n",
       ecam.physical, bytes, ecam.bus_count - 1);
  return ecam.bus_count;
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

  kernel_space.root = bootstrap_new_table();
  map_kernel_section(boot, __text_start, __text_end, PAGE_EXEC);
  map_kernel_section(boot, __rodata_start, __rodata_end, 0);
  map_kernel_section(boot, __data_start, __data_end, PAGE_WRITE);
  map_framebuffer(&boot->framebuffer);
  map_local_apic();
  map_hpet();
  unsigned pci_buses = map_pci_ecam(boot);

  for (size_t offset = 0; offset < metadata_bytes; offset += PAGE_SIZE) {
    bootstrap_map(PMM_METADATA_BASE + offset, plan.metadata_phys + offset,
                  PAGE_WRITE);
  }

  /* Every CPU's scratch leaves start absent, but each ancestor is allocated now,
   * before any root copies the shared kernel slots. */
  for (size_t cpu = 0; cpu < XAPIC_CPU_LIMIT; ++cpu) {
    for (unsigned slot = 0; slot < SCRATCH_SLOT_COUNT; ++slot) {
      KASSERT(!*bootstrap_leaf(scratch_address(cpu, slot)));
    }
  }

  /* Install the allocation area's ancestors before any roots can share them.
   * This area fits entirely in one PML4 slot, so later growth stays shared. */
  KASSERT(!*bootstrap_leaf(KERNEL_VM_BASE));

  uint64_t *root = bootstrap_pointer(kernel_space.root, PAGE_SIZE);
  KASSERT(!root[RECURSIVE_SLOT]);
  root[RECURSIVE_SLOT] = kernel_space.root | PTE_PRESENT | PTE_WRITE | PTE_NX;

  klog("paging: switching CR3 from 0x%lx to owned root 0x%lx\n",
       read_cr3(), kernel_space.root);
  write_cr3(kernel_space.root);
  /* The bootstrap direct map that the early console drew through is gone. */
  early_console_rebind(boot->framebuffer.address);
  active = true;
  cpu_current()->active_space = &kernel_space;
  pmm_rebase((void *)PMM_METADATA_BASE);
  bootstrap_offset = 0;
  boot->bootstrap_direct_offset = 0;
  arch_pci_init(pci_buses);

  klog("paging: owned CR3=0x%lx; 4 KiB leaves, recursive slot %u, no HHDM\n",
       read_cr3(), RECURSIVE_SLOT);
}

/* IF=0 keeps the caller on this CPU, and off any other task, until it unmaps. */
static void *map_scratch(phys_addr_t physical, unsigned slot)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  size_t cpu = cpu_current()->index;
  KASSERT(active && physical_valid(physical) && slot < SCRATCH_SLOT_COUNT &&
          cpu < XAPIC_CPU_LIMIT && !scratch_busy[cpu][slot]);

  uintptr_t virtual = scratch_address(cpu, slot);
  volatile uint64_t *table = active_table(virtual, LEVEL_PT);
  volatile uint64_t *entry = &table[index_at(virtual, LEVEL_PT)];
  KASSERT(!(*entry & PTE_PRESENT));

  scratch_busy[cpu][slot] = true;
  *entry = physical | PTE_PRESENT | PTE_WRITE | PTE_NX;
  invlpg(virtual);
  return (void *)virtual;
}

static void unmap_scratch(unsigned slot)
{
  size_t cpu = cpu_current()->index;
  KASSERT(slot < SCRATCH_SLOT_COUNT && scratch_busy[cpu][slot]);

  uintptr_t virtual = scratch_address(cpu, slot);
  volatile uint64_t *table = active_table(virtual, LEVEL_PT);

  /* Finish accesses through the slot before withdrawing its mapping. */
  __asm__ volatile("" : : : "memory");
  table[index_at(virtual, LEVEL_PT)] = 0;
  invlpg(virtual);
  scratch_busy[cpu][slot] = false;
}

void arch_frame_zero(phys_addr_t physical)
{
  memset(map_scratch(physical, SCRATCH_DATA), 0, PAGE_SIZE);
  unmap_scratch(SCRATCH_DATA);
}

static uint64_t read_table_entry(phys_addr_t physical, unsigned index)
{
  volatile uint64_t *table = map_scratch(physical, SCRATCH_TABLE);
  uint64_t value = table[index];
  unmap_scratch(SCRATCH_TABLE);
  return value;
}

static void write_table_entry(phys_addr_t physical, unsigned index, uint64_t value)
{
  volatile uint64_t *table = map_scratch(physical, SCRATCH_TABLE);
  table[index] = value;
  unmap_scratch(SCRATCH_TABLE);
}

static unsigned entry_permissions(uint64_t entry)
{
  unsigned permissions = 0;
  if (entry & PTE_WRITE) {
    permissions |= PAGE_WRITE;
  }
  if (!(entry & PTE_NX)) {
    permissions |= PAGE_EXEC;
  }
  if (entry & PTE_USER) {
    permissions |= PAGE_USER;
  }
  return permissions;
}

static bool affects_active_space(const struct arch_address_space *space,
                                 uintptr_t virtual)
{
  return space == cpu_current()->active_space || virtual >= HIGHER_HALF_BASE;
}

/* Scratch mappings reach either root without changing the executing address
 * space. No scratch pointer survives a helper call or a descent to a child. */
static enum mm_result walk_to_leaf(const struct arch_address_space *space,
                                   uintptr_t virtual, bool create,
                                   phys_addr_t *leaf_table, unsigned *allowed)
{
  phys_addr_t physical = space->root;
  *allowed = PAGE_WRITE | PAGE_EXEC | PAGE_USER;

  for (unsigned level = LEVEL_PML4; level > LEVEL_PT; --level) {
    unsigned index = index_at(virtual, level);
    uint64_t value = read_table_entry(physical, index);
    if (!(value & PTE_PRESENT)) {
      if (!create) {
        return MM_NOT_MAPPED;
      }

      phys_addr_t child = pmm_alloc(1);
      if (!child) {
        return MM_NO_MEMORY;
      }

      arch_frame_zero(child);
      value = child | PTE_PRESENT | PTE_WRITE;
      if (virtual <= LOWER_HALF_MAX) {
        value |= PTE_USER;
      }
      write_table_entry(physical, index, value);

      /* New ancestors also change recursive aliases. Reload the actual active
       * root, including when modifying kernel tables shared with that root. */
      if (affects_active_space(space, virtual)) {
        write_cr3(cpu_current()->active_space->root);
      }
    }

    if ((value & PTE_LARGE) || !physical_valid(value & PTE_ADDRESS_MASK)) {
      panic("corrupt or unsupported page-table ancestor at %p", (void *)virtual);
    }

    *allowed &= entry_permissions(value);
    physical = value & PTE_ADDRESS_MASK;
  }

  *leaf_table = physical;
  return MM_OK;
}

static enum mm_result map_page(struct arch_address_space *space,
                               uintptr_t virtual, phys_addr_t physical,
                               unsigned permissions, uint64_t cache_flags)
{
  if (!mutable_address(space, virtual) || !physical_valid(physical) ||
      !permissions_valid(permissions) ||
      (space == &kernel_space && (permissions & PAGE_USER))) {
    return MM_INVALID;
  }

  phys_addr_t table;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(space, virtual, true, &table, &allowed);
  if (result != MM_OK) {
    return result;
  }
  unsigned index = index_at(virtual, LEVEL_PT);
  if (read_table_entry(table, index) & PTE_PRESENT) {
    return MM_COLLISION;
  }
  if (permissions & ~allowed) {
    return MM_INVALID;
  }

  write_table_entry(table, index, physical | leaf_flags(permissions) | cache_flags);
  if (affects_active_space(space, virtual)) {
    invlpg(virtual);
  }
  return MM_OK;
}

enum mm_result arch_page_map(struct arch_address_space *space,
                             uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions)
{
  return map_page(space, virtual, physical, permissions, 0);
}

enum mm_result arch_page_map_mmio(uintptr_t virtual, phys_addr_t physical)
{
  return map_page(&kernel_space, virtual, physical, PAGE_WRITE,
                  PTE_CACHE_DISABLE | PTE_WRITE_THROUGH);
}

void paging_pci_config_writable(uintptr_t virtual, bool writable)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(active && virtual >= PCI_ECAM_BASE &&
          virtual - PCI_ECAM_BASE < PCI_ECAM_SIZE && !(virtual & (PAGE_SIZE - 1)));
  volatile uint64_t *entry = &active_table(virtual, LEVEL_PT)[index_at(virtual, LEVEL_PT)];
  KASSERT(*entry & PTE_PRESENT);
  if (writable) {
    *entry |= PTE_WRITE;
  } else {
    *entry &= ~PTE_WRITE;
  }
  invlpg(virtual);
}

enum mm_result arch_page_unmap(struct arch_address_space *space,
                               uintptr_t virtual, phys_addr_t *physical)
{
  if (!mutable_address(space, virtual) || !physical) {
    return MM_INVALID;
  }

  phys_addr_t table;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(space, virtual, false, &table, &allowed);
  if (result != MM_OK) {
    return result;
  }
  unsigned index = index_at(virtual, LEVEL_PT);
  uint64_t value = read_table_entry(table, index);
  if (!(value & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }

  *physical = value & PTE_ADDRESS_MASK;
  write_table_entry(table, index, 0);
  if (affects_active_space(space, virtual)) {
    invlpg(virtual);
  }
  return MM_OK;
}

enum mm_result arch_page_protect(struct arch_address_space *space,
                                 uintptr_t virtual, unsigned permissions)
{
  if (!mutable_address(space, virtual) || !permissions_valid(permissions) ||
      (space == &kernel_space && (permissions & PAGE_USER))) {
    return MM_INVALID;
  }

  phys_addr_t table;
  unsigned allowed;
  enum mm_result result = walk_to_leaf(space, virtual, false, &table, &allowed);
  if (result != MM_OK) {
    return result;
  }
  unsigned index = index_at(virtual, LEVEL_PT);
  uint64_t value = read_table_entry(table, index);
  if (!(value & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if (permissions & ~allowed) {
    return MM_INVALID;
  }

  uint64_t cache_flags = value & (PTE_PAT_4K | PTE_CACHE_DISABLE | PTE_WRITE_THROUGH);
  if (cache_flags && (permissions & (PAGE_USER | PAGE_EXEC))) {
    return MM_INVALID;
  }
  write_table_entry(table, index,
                    (value & PTE_ADDRESS_MASK) | leaf_flags(permissions) | cache_flags);
  if (affects_active_space(space, virtual)) {
    invlpg(virtual);
  }
  return MM_OK;
}

enum mm_result arch_page_query(const struct arch_address_space *space,
                               uintptr_t virtual, struct page_translation *result)
{
  if (!space_valid(space) || !canonical(virtual) || !result) {
    return MM_INVALID;
  }

  phys_addr_t table;
  unsigned allowed;
  enum mm_result status = walk_to_leaf(space, virtual, false, &table, &allowed);
  if (status != MM_OK) {
    return status;
  }

  uint64_t value = read_table_entry(table, index_at(virtual, LEVEL_PT));
  if (!(value & PTE_PRESENT)) {
    return MM_NOT_MAPPED;
  }
  if (!physical_valid(value & PTE_ADDRESS_MASK)) {
    panic("corrupt page-table leaf at %p", (void *)virtual);
  }

  *result = (struct page_translation){
    .physical = (value & PTE_ADDRESS_MASK) | (virtual & (PAGE_SIZE - 1)),
    .permissions = allowed & entry_permissions(value),
  };
  return MM_OK;
}

bool arch_user_buffer_accessible(const struct arch_address_space *space,
                                 uintptr_t address, size_t bytes, bool write)
{
  if (!space_valid(space) || space == &kernel_space || !arch_space_active(space)) {
    return false;
  }
  if (!bytes) {
    return true;
  }
  if (address < PAGE_SIZE || address > LOWER_HALF_MAX ||
      bytes - 1 > LOWER_HALF_MAX - address) {
    return false;
  }

  uintptr_t page = address & ~(PAGE_SIZE - 1);
  uintptr_t last_page = (address + bytes - 1) & ~(PAGE_SIZE - 1);
  uint64_t required = PTE_PRESENT | PTE_USER | (write ? PTE_WRITE : 0);

  for (;;) {
    /* Validate each ancestor before touching the next recursive alias.
     * User and write access must be allowed at every level, not just PT. */
    for (unsigned level = LEVEL_PML4; level >= LEVEL_PT; --level) {
      uint64_t entry = active_table(page, level)[index_at(page, level)];
      if ((entry & required) != required ||
          !physical_valid(entry & PTE_ADDRESS_MASK) ||
          (level > LEVEL_PT && (entry & PTE_LARGE))) {
        return false;
      }
    }
    if (page == last_page) {
      return true;
    }
    page += PAGE_SIZE;
  }
}

struct arch_address_space *arch_kernel_space(void)
{
  return &kernel_space;
}

enum mm_result arch_space_create(struct arch_address_space *space)
{
  if (!active || !space || space->root) {
    return MM_INVALID;
  }

  phys_addr_t root = pmm_alloc(1);
  if (!root) {
    return MM_NO_MEMORY;
  }
  arch_frame_zero(root);

  volatile uint64_t *source = map_scratch(kernel_space.root, SCRATCH_TABLE);
  volatile uint64_t *target = map_scratch(root, SCRATCH_DATA);
  unsigned first_kernel_slot = index_at(HIGHER_HALF_BASE, LEVEL_PML4);
  for (unsigned i = first_kernel_slot; i <= TABLE_INDEX_MASK; ++i) {
    if (i != RECURSIVE_SLOT) {
      target[i] = source[i];
    }
  }
  target[RECURSIVE_SLOT] = root | PTE_PRESENT | PTE_WRITE | PTE_NX;
  unmap_scratch(SCRATCH_DATA);
  unmap_scratch(SCRATCH_TABLE);

  space->root = root;
  return MM_OK;
}

bool arch_space_active(const struct arch_address_space *space)
{
  return space && space == cpu_current()->active_space;
}

enum mm_result arch_space_activate(struct arch_address_space *space)
{
  if (!space_valid(space)) {
    return MM_INVALID;
  }

  /* A CPU never changes roots while one of its own scratch slots is mapped. */
  size_t cpu = cpu_current()->index;
  KASSERT(!scratch_busy[cpu][SCRATCH_TABLE] && !scratch_busy[cpu][SCRATCH_DATA]);
  /* Kernel mappings, including this stack, stay identical across the switch.
   * PCID and global translations remain disabled, so CR3 flushes the old TLB. */
  write_cr3(space->root);
  cpu_current()->active_space = space;
  return MM_OK;
}

static void free_private_tables(phys_addr_t physical, unsigned level)
{
  if (level > LEVEL_PT) {
    unsigned entries = level == LEVEL_PML4 ?
      index_at(HIGHER_HALF_BASE, LEVEL_PML4) : TABLE_INDEX_MASK + 1;

    for (unsigned i = 0; i < entries; ++i) {
      uint64_t entry = read_table_entry(physical, i);
      if (entry & PTE_PRESENT) {
        KASSERT(!(entry & PTE_LARGE));
        free_private_tables(entry & PTE_ADDRESS_MASK, level - 1);
      }
    }
  }
  pmm_free(physical, 1);
}

enum mm_result arch_space_destroy(struct arch_address_space *space)
{
  if (!space_valid(space) || space == &kernel_space || arch_space_active(space)) {
    return MM_INVALID;
  }

  free_private_tables(space->root, LEVEL_PML4);
  space->root = 0;
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

uintptr_t arch_user_vm_base(void)
{
  return PAGE_SIZE;
}

size_t arch_user_vm_size(void)
{
  return LOWER_HALF_MAX + 1 - PAGE_SIZE;
}
