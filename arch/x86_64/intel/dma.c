#include <arch/acpi.h>
#include <arch/cpu.h>
#include <arch/intel/dma.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/mm/vm.h>
#include <kernel/pci/registers.h>

/* Intel VT-d rev. 5.0 (D51397-017), sections 8.1-8.9 and 11.4.5; Linux v6.19
 * 05f7e89a include/acpi/actbl1.h and drivers/iommu/intel/{dmar.c,iommu.h}.
 * Local bounded parser: no Linux IOMMU manager or firmware workaround. */
#define DMAR_MAX_BYTES (64u * 1024u)
#define DMAR_HEADER_BYTES 48u
#define DMAR_MAX_UNITS 32u
#define DMAR_MAX_SCOPES 256u
#define DMAR_INCLUDE_ALL 1u
#define DMAR_SCOPE_HEADER_BYTES 6u
#define DMAR_SCOPE_ENDPOINT 1u
#define DMAR_SCOPE_BRIDGE 2u
#define DMAR_SCOPE_IOAPIC 3u
#define DMAR_SCOPE_HPET 4u
#define DMAR_SCOPE_NAMESPACE 5u
#define DMAR_VERSION 0u
#define DMAR_GLOBAL_STATUS 0x1cu
#define DMAR_TRANSLATION_ENABLED (1u << 31)

enum dmar_structure {
  DMAR_HARDWARE_UNIT,
  DMAR_RESERVED_MEMORY,
  DMAR_ROOT_ATS,
  DMAR_HARDWARE_AFFINITY,
  DMAR_NAMESPACE,
  DMAR_SATC,
  DMAR_SIDP,
};

static uint8_t dmar_copy[DMAR_MAX_BYTES];
static size_t dmar_bytes;
static const char *capture_failure = "DMAR not captured";
static bool mapping_retained;
static uintptr_t retained_window;

struct remapper {
  uint64_t physical, bytes;
  size_t offset, length;
  uint16_t segment;
  bool include_all;
};

static uint16_t read_u16(const uint8_t *bytes)
{
  uint16_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static uint64_t read_u64(const uint8_t *bytes)
{
  uint64_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

void intel_dma_capture_boot(const struct boot_info *boot)
{
  size_t bytes;
  const void *table = acpi_boot_table(boot, "DMAR", &bytes);
  dmar_bytes = 0;
  capture_failure = "DMAR absent, duplicate or invalid";
  if (!table) {
    return;
  }
  if (bytes > sizeof(dmar_copy)) {
    capture_failure = "DMAR exceeds 64 KiB capture bound";
    return;
  }
  if (bytes < DMAR_HEADER_BYTES) {
    capture_failure = "DMAR header truncated";
    return;
  }
  memcpy(dmar_copy, table, bytes);
  dmar_bytes = bytes;
  capture_failure = NULL;
}

static bool scopes_valid(const uint8_t *entry, size_t bytes, bool drhd,
                         bool include_all, unsigned *count)
{
  for (size_t offset = 0; offset < bytes;) {
    if (bytes - offset < DMAR_SCOPE_HEADER_BYTES || ++*count > DMAR_MAX_SCOPES) {
      return false;
    }
    const uint8_t *scope = entry + offset;
    size_t length = scope[1];
    if (length < DMAR_SCOPE_HEADER_BYTES + 2 || length > bytes - offset ||
        (length - DMAR_SCOPE_HEADER_BYTES) % 2) {
      return false;
    }
    if (drhd && (scope[0] < DMAR_SCOPE_ENDPOINT || scope[0] > DMAR_SCOPE_NAMESPACE ||
          scope[2] || scope[3] ||
          (scope[0] <= DMAR_SCOPE_BRIDGE && (include_all || scope[4])) ||
          (scope[0] >= DMAR_SCOPE_HPET && length != DMAR_SCOPE_HEADER_BYTES + 2))) {
      return false;
    }
    for (size_t path = DMAR_SCOPE_HEADER_BYTES; path < length; path += 2) {
      if (scope[path] >= PCI_DEVICE_COUNT || scope[path + 1] >= PCI_FUNCTION_COUNT) {
        return false;
      }
    }
    offset += length;
  }
  return true;
}

static bool parse_units(struct remapper units[DMAR_MAX_UNITS], unsigned *count)
{
  *count = 0;
  unsigned scopes = 0;
  if (dmar_copy[8] != 1 || dmar_copy[36] < 31 || dmar_copy[36] > 63) {
    return false;
  }
  for (size_t offset = DMAR_HEADER_BYTES; offset < dmar_bytes;) {
    if (dmar_bytes - offset < 4) {
      return false;
    }
    const uint8_t *entry = dmar_copy + offset;
    unsigned type = read_u16(entry);
    size_t length = read_u16(entry + 2);
    if (length < 4 || length > dmar_bytes - offset) {
      return false;
    }
    if (type == DMAR_HARDWARE_UNIT) {
      if (length < 16 || *count == DMAR_MAX_UNITS || (entry[4] & ~DMAR_INCLUDE_ALL) ||
          (entry[5] & 0xf0)) {
        return false;
      }
      struct remapper unit = {
        .physical = read_u64(entry + 8),
        .bytes = PAGE_SIZE << entry[5],
        .segment = read_u16(entry + 6),
        .offset = offset + 16, .length = length - 16,
        .include_all = !!(entry[4] & DMAR_INCLUDE_ALL),
      };
      if (!unit.physical || (unit.physical & (unit.bytes - 1)) ||
          unit.bytes > UINT64_MAX - unit.physical ||
          !scopes_valid(entry + 16, length - 16, true, unit.include_all, &scopes)) {
        return false;
      }
      for (unsigned i = 0; i < *count; ++i) {
        const struct remapper *prior = &units[i];
        if ((prior->segment == unit.segment && prior->include_all) ||
            (prior->physical < unit.physical + unit.bytes &&
             unit.physical < prior->physical + prior->bytes)) {
          return false;
        }
      }
      units[(*count)++] = unit;
    } else {
      /* These definitions describe reserved memory, ATS, affinity, names and
       * properties. They do not assign PCI requesters to remapping units. */
      static const uint8_t minimum[] = {
        [DMAR_RESERVED_MEMORY] = 24, [DMAR_ROOT_ATS] = 8,
        [DMAR_HARDWARE_AFFINITY] = 20, [DMAR_NAMESPACE] = 9,
        [DMAR_SATC] = 8, [DMAR_SIDP] = 8,
      };
      if (type >= sizeof(minimum) || length < minimum[type]) {
        return false;
      }
      if ((type == DMAR_RESERVED_MEMORY || type == DMAR_ROOT_ATS ||
            type == DMAR_SATC || type == DMAR_SIDP) &&
          !scopes_valid(entry + minimum[type], length - minimum[type],
            false, false, &scopes)) {
        return false;
      }
      if ((type == DMAR_HARDWARE_AFFINITY && length != minimum[type]) ||
          (type == DMAR_NAMESPACE && entry[length - 1])) {
        return false;
      }
    }
    offset += length;
  }
  return *count != 0;
}

static bool address_equal(struct pci_address first, struct pci_address second)
{
  return first.bus == second.bus && first.device == second.device &&
      first.function == second.function;
}

static bool pci_path(struct pci_address address,
                     const struct pci_device *path[PCI_BUS_COUNT], unsigned *depth)
{
  *depth = 0;
  const struct pci_device *device = NULL;
  if (address.bus >= arch_pci_bus_count() || address.device >= PCI_DEVICE_COUNT ||
      address.function >= PCI_FUNCTION_COUNT || pci_inventory_state() != PCI_INVENTORY_COMPLETE) {
    return false;
  }
  for (size_t i = 0; i < pci_device_count(); ++i) {
    const struct pci_device *candidate = pci_device_at(i);
    if (address_equal(candidate->address, address)) {
      if (device) {
        return false;
      }
      device = candidate;
    }
  }
  if (!device || device->header_type != PCI_HEADER_ENDPOINT) {
    return false;
  }
  while (device) {
    if (*depth == PCI_BUS_COUNT ||
        pci_read16(device->address, PCI_VENDOR_ID) != device->vendor_id ||
        pci_read16(device->address, PCI_DEVICE_ID) != device->device_id ||
        (pci_read8(device->address, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) != device->header_type) {
      return false;
    }
    path[(*depth)++] = device;
    const struct pci_device *parent = device->parent;
    if (parent && (parent->header_type != PCI_HEADER_BRIDGE ||
          parent->base_class != PCI_CLASS_BRIDGE || parent->subclass != PCI_SUBCLASS_PCI_BRIDGE ||
          parent->address.bus >= device->address.bus || parent->secondary_bus != device->address.bus ||
          parent->subordinate_bus < device->address.bus ||
          pci_read8(parent->address, PCI_BRIDGE_PRIMARY) != parent->address.bus ||
          pci_read8(parent->address, PCI_BRIDGE_SECONDARY) != parent->secondary_bus ||
          pci_read8(parent->address, PCI_BRIDGE_SUBORDINATE) != parent->subordinate_bus)) {
      return false;
    }
    if (!parent && device->address.bus) {
      return false;
    }
    device = parent;
  }
  for (unsigned i = 0; i < *depth / 2; ++i) {
    const struct pci_device *swap = path[i];
    path[i] = path[*depth - i - 1];
    path[*depth - i - 1] = swap;
  }
  return true;
}

static int scope_match(const uint8_t *scope, const struct pci_device *const *path,
                       unsigned depth)
{
  unsigned type = scope[0];
  if (type == DMAR_SCOPE_IOAPIC || type == DMAR_SCOPE_HPET) {
    return 0;
  }
  unsigned pairs = (scope[1] - DMAR_SCOPE_HEADER_BYTES) / 2;
  if (type == DMAR_SCOPE_NAMESPACE) {
    struct pci_address source = {scope[5], scope[6], scope[7]};
    /* Namespace source IDs can alias PCI requesters; do not invent an ACPI
     * device/PCI ownership equivalence for such a collision. */
    return address_equal(source, path[depth - 1]->address) ? -1 : 0;
  }
  for (unsigned start = 0; start < depth; ++start) {
    if (path[start]->address.bus != scope[5]) {
      continue;
    }
    unsigned available = depth - start;
    unsigned i = 0;
    for (; i < pairs && i < available; ++i) {
      struct pci_address address = path[start + i]->address;
      if (address.device != scope[6 + 2 * i] || address.function != scope[7 + 2 * i]) {
        break;
      }
    }
    if (i == available && pairs > available) {
      return -1;
    }
    if (i != pairs) {
      continue;
    }
    const struct pci_device *last = path[start + pairs - 1];
    if (type == DMAR_SCOPE_ENDPOINT) {
      return last->header_type == PCI_HEADER_ENDPOINT ? 1 : -1;
    }
    return last->header_type == PCI_HEADER_BRIDGE ? 1 : -1;
  }
  return 0;
}

static bool select_unit(const struct remapper *units, unsigned count,
                         const struct pci_device *const *path, unsigned depth,
                         const struct remapper **selected)
{
  const struct remapper *fallback = NULL;
  *selected = NULL;
  for (unsigned i = 0; i < count; ++i) {
    const struct remapper *unit = &units[i];
    if (unit->segment) {
      continue;
    }
    if (unit->include_all) {
      fallback = unit;
    }
    for (size_t offset = 0; offset < unit->length;) {
      const uint8_t *scope = dmar_copy + unit->offset + offset;
      int match = scope_match(scope, path, depth);
      if (match < 0 || (match && *selected)) {
        return false;
      }
      if (match) {
        *selected = unit;
      }
      offset += scope[1];
    }
  }
  if (!*selected) {
    *selected = fallback;
  }
  return *selected != NULL;
}

static bool page_available(const struct boot_info *boot, uint64_t physical)
{
  if (!arch_pci_mmio_available(physical, PAGE_SIZE) ||
      vm_kernel_physical_overlap(physical, PAGE_SIZE) ||
      boot->region_count > BOOT_MAX_REGIONS ||
      boot->framebuffer.size > UINT64_MAX - boot->framebuffer.physical ||
      (boot->framebuffer.size && physical < boot->framebuffer.physical + boot->framebuffer.size &&
       boot->framebuffer.physical < physical + PAGE_SIZE)) {
    return false;
  }
  for (size_t i = 0; i < boot->region_count; ++i) {
    const struct boot_region *region = &boot->regions[i];
    if (region->length > UINT64_MAX - region->base ||
        (region->type != BOOT_RESERVED && physical < region->base + region->length &&
         region->base < physical + PAGE_SIZE)) {
      return false;
    }
  }
  return true;
}

static bool release_window(uintptr_t window)
{
  struct page_translation translation;
  enum mm_result query = vm_query(vm_kernel_space(), window, &translation);
  if (query == MM_OK) {
    phys_addr_t physical;
    if (vm_unmap(vm_kernel_space(), window, &physical) != MM_OK) {
      goto retain;
    }
  } else if (query != MM_NOT_MAPPED) {
    goto retain;
  }
  if (vm_release(vm_kernel_space(), window, PAGE_SIZE) == MM_OK) {
    return true;
  }
retain:
  mapping_retained = true;
  retained_window = window;
  vm_kernel_quarantine(window, PAGE_SIZE);
  kerror("intel-dma: temporary remapper mapping/reservation retained at %lx; admission fenced\n",
      retained_window);
  return false;
}

bool intel_dma_identity(const struct boot_info *boot, struct pci_address address)
{
  if (!boot || cpu_interrupts_enabled() || arch_cpu_index() || arch_cpu_count() ||
      vm_kernel_runtime() || mapping_retained) {
    return false;
  }
  if (capture_failure) {
    kinfo("intel-dma: identity DMA unavailable: %s\n", capture_failure);
    return false;
  }
  struct remapper units[DMAR_MAX_UNITS];
  const struct pci_device *path[PCI_BUS_COUNT];
  unsigned count, depth;
  const struct remapper *unit;
  if (!parse_units(units, &count) || !pci_path(address, path, &depth) ||
      !select_unit(units, count, path, depth, &unit)) {
    kinfo("intel-dma: identity DMA unavailable: malformed, unsupported or ambiguous DMAR/PCI coverage\n");
    return false;
  }
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return false;
  }
  uintptr_t window = 0;
  bool admitted = false;
  if (page_available(boot, unit->physical) &&
      vm_reserve(vm_kernel_space(), PAGE_SIZE, PAGE_SIZE, &window) == MM_OK) {
    if (vm_map_mmio(window, unit->physical) == MM_OK &&
        vm_protect(vm_kernel_space(), window, 0) == MM_OK) {
      uint32_t version = *(const volatile uint32_t *)(window + DMAR_VERSION);
      uint32_t first = *(const volatile uint32_t *)(window + DMAR_GLOBAL_STATUS);
      uint32_t second = *(const volatile uint32_t *)(window + DMAR_GLOBAL_STATUS);
      admitted = version && version != UINT32_MAX && first == second &&
          !(second & DMAR_TRANSLATION_ENABLED);
      kinfo("intel-dma: segment=0 PCI=%x:%x.%u DRHD=%lx version=%x GSTS=%x/%x identity=%u\n",
          address.bus, address.device, address.function, unit->physical, version, first, second, admitted);
    } else {
      kinfo("intel-dma: identity DMA unavailable: remapper RO/UC mapping failed\n");
    }
    if (!release_window(window)) {
      admitted = false;
    }
  } else {
    kinfo("intel-dma: identity DMA unavailable: remapper MMIO mapping prerequisite\n");
  }
  vm_kernel_owner_release(&owner);
  return admitted;
}
