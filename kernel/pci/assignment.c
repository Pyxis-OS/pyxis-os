/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/cpu.h>
#include <arch/pci.h>
#include <kernel/acpi/pci_resources.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/assignment.h>
#include <kernel/pci/placement.h>
#include <kernel/pci/registers.h>

#define BELOW_4G UINT64_C(0x100000000)
#define PCI_BAR32_MAX_BYTES UINT64_C(0x80000000)
#define PCI_BAR64_MAX_BYTES (UINT64_C(1) << 63)
#define PCI_ROM_MAX_BYTES UINT64_C(0x80000000)
#define PCI_PLACEMENT_CHECK_LIMIT 4096u
#define PCI_ROM_ENDPOINT 0x30
#define PCI_ROM_BRIDGE 0x38
#define PCI_ROM_ENABLE 1u
#define PCI_ROM_ADDRESS_MASK UINT32_C(0xfffff800)
#define PCI_BRIDGE_MEMORY 0x20
#define PCI_BRIDGE_PREFETCH 0x24
#define PCI_BRIDGE_PREFETCH_BASE_HIGH 0x28
#define PCI_BRIDGE_PREFETCH_LIMIT_HIGH 0x2c
#define PCI_BRIDGE_WINDOW_ADDRESS_MASK 0xfff0u
#define PCI_BRIDGE_WINDOW_TYPE_MASK 0xfu
#define PCI_EXT_CAP_REBAR 0x15
#define PCI_EXT_CAP_SRIOV 0x10
#define PCI_SRIOV_CONTROL 8
#define PCI_SRIOV_ENABLE 1u
#define PCI_SRIOV_MEMORY_ENABLE 8u
#define PCI_SRIOV_BAR_FIRST 0x24
#define PCI_CAP_ENHANCED_ALLOCATION 0x14
#define PCI_EXPRESS_CAP_FLAGS 2
#define PCI_EXPRESS_CAP_VERSION_MASK 0xfu
#define PCI_EXPRESS_DEVICE_CONTROL_2 0x28
#define PCI_EXPRESS_ARI_FORWARD (1u << 5)
#define PCI_REBAR_CAPABILITY 4
#define PCI_REBAR_CONTROL 8
#define PCI_REBAR_ENTRY_BYTES 8
#define PCI_REBAR_BAR_MASK 7u
#define PCI_REBAR_COUNT_SHIFT 5
#define PCI_REBAR_COUNT_MASK 7u
#define PCI_REBAR_SIZE_SHIFT 8
#define PCI_REBAR_SIZE_MASK 0x3fu
#define PCI_REBAR_MAX_SIZE_CODE 27u
#define PCI_REBAR_SUPPORT_SHIFT 4
#define PCI_REBAR_SIZE_BASE_SHIFT 20

struct interval { uint64_t first, last; };
struct allocation {
  struct allocation *next;
  const struct pci_device *device;
  uint64_t generation;
  unsigned bar;
  struct interval extent;
  bool reserved;
};
struct proof {
  struct interval *excluded;
  size_t count, capacity;
  const struct pci_device *path[PCI_BUS_COUNT];
  size_t depth, root;
  const char *failure;
};
struct decoded_bar {
  uint64_t base, bytes;
  bool memory, memory64, upper;
};

static struct boot_region memory_regions[BOOT_MAX_REGIONS];
static size_t memory_region_count;
static struct boot_efi_region efi_regions[BOOT_MAX_REGIONS];
static size_t efi_region_count;
static bool firmware_map_complete;
static bool prepared;
static bool placement_uncertain;
static struct allocation *allocations;

static bool fail(struct proof *proof, const char *reason)
{
  proof->failure = reason;
  return false;
}

static bool exclude(struct proof *proof, uint64_t first, uint64_t last)
{
  if (first > last || proof->count == proof->capacity) {
    return fail(proof, "exclusion extent/capacity");
  }
  proof->excluded[proof->count++] = (struct interval){first, last};
  return true;
}

static bool exclude_size(struct proof *proof, uint64_t base, uint64_t bytes)
{
  if (!bytes || bytes - 1 > UINT64_MAX - base) {
    return fail(proof, "allocation extent overflow");
  }
  return exclude(proof, base, base + bytes - 1);
}

void pci_assignment_prepare(const struct boot_info *boot)
{
  KASSERT(!cpu_interrupts_enabled() && !prepared);
  KASSERT(boot->region_count <= BOOT_MAX_REGIONS);
  memory_region_count = boot->region_count;
  memcpy(memory_regions, boot->regions, memory_region_count * sizeof(*memory_regions));
  firmware_map_complete = boot->efi_map_valid && boot->efi_region_count &&
      boot->efi_region_count <= BOOT_MAX_REGIONS;
  if (firmware_map_complete) {
    efi_region_count = boot->efi_region_count;
    memcpy(efi_regions, boot->efi_regions, efi_region_count * sizeof(*efi_regions));
  }
  prepared = true;
}

enum mm_result pci_assignment_window_prepare(struct pci_claim *claim, size_t bytes,
    struct pci_assignment_window *window)
{
  KASSERT(!cpu_interrupts_enabled());
  if (!prepared || !claim || !claim->device || claim->device->owner != claim ||
      !claim->generation || !window || window->base || !bytes || (bytes & (PAGE_SIZE - 1))) {
    return MM_INVALID;
  }
  uintptr_t base;
  enum mm_result status = vm_reserve(vm_kernel_space(), bytes, PAGE_SIZE, &base);
  if (status == MM_OK) {
    *window = (struct pci_assignment_window){.claim = claim, .generation = claim->generation,
        .base = base, .bytes = bytes};
    /* Even an unfilled window is native-owner boot-lifetime storage. */
    claim->assignment_pinned = true;
  }
  return status;
}

static bool on_path(const struct proof *proof, const struct pci_device *device)
{
  for (size_t i = 0; i < proof->depth; ++i) {
    if (proof->path[i] == device) {
      return true;
    }
  }
  return false;
}

static bool find_route(struct proof *proof, const struct acpi_pci_resources *resources,
    const struct pci_device *device)
{
  unsigned bus = device->address.bus;
  for (;;) {
    const struct pci_device *parent = NULL;
    for (size_t i = 0; i < pci_device_count(); ++i) {
      const struct pci_device *other = pci_device_at(i);
      if (other->header_type != PCI_HEADER_BRIDGE ||
          pci_read8(other->address, PCI_BRIDGE_SECONDARY) != bus) {
        continue;
      }
      if (parent || pci_read8(other->address, PCI_BRIDGE_PRIMARY) != other->address.bus ||
          other->address.bus >= bus ||
          pci_read8(other->address, PCI_BRIDGE_SUBORDINATE) < device->address.bus ||
          proof->depth == PCI_BUS_COUNT) {
        return fail(proof, "ambiguous or invalid ancestor bridge");
      }
      parent = other;
    }
    if (!parent) {
      break;
    }
    proof->path[proof->depth++] = parent;
    bus = parent->address.bus;
  }
  bool found = false;
  for (size_t i = 0; i < resources->root_count; ++i) {
    const struct acpi_pci_root *root = &resources->roots[i];
    if (root->segment || root->first_bus != bus || root->last_bus < device->address.bus) {
      continue;
    }
    if (found) {
      return fail(proof, "ambiguous ACPI root route");
    }
    proof->root = i;
    found = true;
  }
  return found || fail(proof, "no matching live ACPI root");
}

static bool bridge_window(struct proof *proof, const struct pci_device *bridge,
    bool prefetch, struct interval *range, bool *enabled)
{
  uint32_t value = pci_read32(bridge->address,
      prefetch ? PCI_BRIDGE_PREFETCH : PCI_BRIDGE_MEMORY);
  unsigned base_type = value & PCI_BRIDGE_WINDOW_TYPE_MASK;
  unsigned limit_type = (value >> 16) & PCI_BRIDGE_WINDOW_TYPE_MASK;
  if (value == UINT32_MAX || base_type != limit_type || base_type > (prefetch ? 1u : 0u)) {
    return fail(proof, "unsupported bridge memory window encoding");
  }
  uint64_t first = (uint64_t)(value & PCI_BRIDGE_WINDOW_ADDRESS_MASK) << 16;
  uint64_t last = (uint64_t)((value >> 16) & PCI_BRIDGE_WINDOW_ADDRESS_MASK) << 16;
  if (prefetch && base_type == 1) {
    first |= (uint64_t)pci_read32(bridge->address, PCI_BRIDGE_PREFETCH_BASE_HIGH) << 32;
    last |= (uint64_t)pci_read32(bridge->address, PCI_BRIDGE_PREFETCH_LIMIT_HIGH) << 32;
  }
  last |= UINT64_C(0xfffff);
  *range = (struct interval){first, last};
  *enabled = first <= last;
  return true;
}

/* D3hot cannot decode MMIO. Other power states stay conservative. */
static bool decode_active(struct proof *proof, const struct pci_device *device, bool *active,
    bool *express)
{
  uint16_t command = pci_read16(device->address, PCI_COMMAND);
  if (command == UINT16_MAX) {
    return fail(proof, "inaccessible PCI command");
  }
  *active = (command & PCI_COMMAND_MEMORY) != 0;
  *express = false;
  if (!(pci_read16(device->address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return true;
  }
  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned power = 0;
  unsigned express_offset = 0, express_bytes = 0;
  unsigned offset = pci_read8(device->address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      return fail(proof, "invalid conventional capability chain");
    }
    seen[offset / PCI_REGISTER_BYTES] = true;
    unsigned id = pci_read8(device->address, offset);
    if (id == PCI_CAP_EXPRESS) {
      if (*express) {
        return fail(proof, "duplicate PCI Express capability");
      }
      *express = true;
      unsigned version = pci_read16(device->address, offset + PCI_EXPRESS_CAP_FLAGS) &
          PCI_EXPRESS_CAP_VERSION_MASK;
      if (device->header_type == PCI_HEADER_BRIDGE && version >= 2 &&
          (offset > PCI_CONVENTIONAL_BYTES - PCI_EXPRESS_DEVICE_CONTROL_2 - 2 ||
           (pci_read16(device->address, offset + PCI_EXPRESS_DEVICE_CONTROL_2) &
            PCI_EXPRESS_ARI_FORWARD))) {
        return fail(proof, "unsupported ARI forwarding topology");
      }
      express_offset = offset;
      express_bytes = device->header_type == PCI_HEADER_BRIDGE && version >= 2 ?
          PCI_EXPRESS_DEVICE_CONTROL_2 + 2 : PCI_EXPRESS_CAP_FLAGS + 2;
    }
    if (id == PCI_CAP_ENHANCED_ALLOCATION) {
      return fail(proof, "unsupported Enhanced Allocation resource encoding");
    }
    if (id == PCI_CAP_POWER) {
      if (power || offset > PCI_CONVENTIONAL_BYTES - PCI_POWER_BYTES) {
        return fail(proof, "invalid PCI PM capability");
      }
      uint16_t pmcsr = pci_read16(device->address, offset + PCI_POWER_CONTROL);
      if (pmcsr == UINT16_MAX) {
        return fail(proof, "inaccessible PCI PMCSR");
      }
      *active = *active && (pmcsr & PCI_POWER_STATE_MASK) != PCI_POWER_D3HOT;
      power = offset;
    }
    offset = pci_read8(device->address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }
  if (power) {
    for (unsigned other = power + PCI_REGISTER_BYTES; other < power + PCI_POWER_BYTES;
        other += PCI_REGISTER_BYTES) {
      if (seen[other / PCI_REGISTER_BYTES]) {
        return fail(proof, "PCI PM capability overlaps another header");
      }
    }
  }
  for (unsigned other = express_offset + PCI_REGISTER_BYTES;
      express_offset && other < express_offset + express_bytes; other += PCI_REGISTER_BYTES) {
    if (seen[other / PCI_REGISTER_BYTES]) {
      return fail(proof, "PCI Express fields overlap another capability header");
    }
  }
  return true;
}

static bool decode_bars(struct proof *proof, const struct pci_device *device,
    struct decoded_bar bars[PCI_BAR_COUNT])
{
  unsigned count = device->header_type == PCI_HEADER_ENDPOINT ? PCI_ENDPOINT_BARS : PCI_BRIDGE_BARS;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t low = pci_read32(device->address, PCI_BAR_FIRST + i * PCI_REGISTER_BYTES);
    if (low & PCI_BAR_IO) {
      continue;
    }
    unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
    if (type != PCI_BAR_MEMORY_32 && type != PCI_BAR_MEMORY_64) {
      return fail(proof, "unsupported foreign BAR encoding");
    }
    bars[i].memory = true;
    bars[i].memory64 = type == PCI_BAR_MEMORY_64;
    bars[i].base = low & PCI_BAR_MEMORY_ADDRESS_MASK;
    if (bars[i].memory64) {
      if (i + 1 == count) {
        return fail(proof, "foreign 64-bit BAR lacks upper half");
      }
      bars[i].base |= (uint64_t)pci_read32(device->address,
          PCI_BAR_FIRST + (i + 1) * PCI_REGISTER_BYTES) << 32;
      bars[++i].upper = true;
    }
  }
  return true;
}

static bool extended_sizes(struct proof *proof, const struct pci_device *device,
    struct decoded_bar bars[PCI_BAR_COUNT])
{
  uint16_t headers[PCI_CONFIG_BYTES / PCI_REGISTER_BYTES];
  size_t count = 0;
  unsigned rebar = 0, sriov = 0;
  for (unsigned offset = PCI_EXT_CAP_FIRST; offset;) {
    if (offset < PCI_EXT_CAP_FIRST || offset > PCI_CONFIG_BYTES - PCI_REGISTER_BYTES ||
        (offset & (PCI_REGISTER_BYTES - 1))) {
      return fail(proof, "invalid extended capability offset");
    }
    for (size_t i = 0; i < count; ++i) {
      if (headers[i] == offset) {
        return fail(proof, "cyclic extended capability chain");
      }
    }
    if (count == sizeof(headers) / sizeof(headers[0])) {
      return fail(proof, "extended capability capacity");
    }
    headers[count++] = offset;
    uint32_t header = pci_read32(device->address, offset);
    if (!header) {
      break;
    }
    if (header == UINT32_MAX) {
      return fail(proof, "inaccessible extended capability");
    }
    unsigned id = header & PCI_EXT_CAP_ID_MASK;
    if (id == PCI_EXT_CAP_REBAR) {
      if (rebar || ((header >> PCI_EXT_CAP_VERSION_SHIFT) & PCI_EXT_CAP_VERSION_MASK) != 1) {
        return fail(proof, "duplicate or unsupported Resizable BAR capability");
      }
      rebar = offset;
    }
    if (id == PCI_EXT_CAP_SRIOV) {
      if (sriov || ((header >> PCI_EXT_CAP_VERSION_SHIFT) & PCI_EXT_CAP_VERSION_MASK) != 1 ||
          offset > PCI_CONFIG_BYTES - PCI_SRIOV_BAR_FIRST - PCI_BAR_COUNT * PCI_REGISTER_BYTES ||
          (pci_read16(device->address, offset + PCI_SRIOV_CONTROL) &
           (PCI_SRIOV_ENABLE | PCI_SRIOV_MEMORY_ENABLE))) {
        return fail(proof, "active or malformed SR-IOV aperture");
      }
      sriov = offset;
      for (unsigned bar = 0; bar < PCI_BAR_COUNT; ++bar) {
        uint32_t low = pci_read32(device->address,
            offset + PCI_SRIOV_BAR_FIRST + bar * PCI_REGISTER_BYTES);
        unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
        if ((low & (PCI_BAR_IO | PCI_BAR_MEMORY_ADDRESS_MASK)) ||
            (type != PCI_BAR_MEMORY_32 && type != PCI_BAR_MEMORY_64)) {
          return fail(proof, "assigned or unsupported SR-IOV BAR");
        }
        if (type == PCI_BAR_MEMORY_64) {
          if (++bar == PCI_BAR_COUNT || pci_read32(device->address,
              offset + PCI_SRIOV_BAR_FIRST + bar * PCI_REGISTER_BYTES)) {
            return fail(proof, "assigned or malformed 64-bit SR-IOV BAR");
          }
        }
      }
    }
    offset = (header >> PCI_EXT_CAP_NEXT_SHIFT) & 0xfff;
  }
  for (size_t i = 0; i < count; ++i) {
    if (sriov && headers[i] > sriov &&
        headers[i] < sriov + PCI_SRIOV_BAR_FIRST + PCI_BAR_COUNT * PCI_REGISTER_BYTES) {
      return fail(proof, "SR-IOV fields overlap another capability header");
    }
  }
  if (!rebar) {
    return true;
  }
  if (rebar > PCI_CONFIG_BYTES - PCI_REBAR_CONTROL - 4) {
    return fail(proof, "truncated Resizable BAR control");
  }
  unsigned entries = (pci_read32(device->address, rebar + PCI_REBAR_CONTROL) >>
      PCI_REBAR_COUNT_SHIFT) & PCI_REBAR_COUNT_MASK;
  if (!entries || entries > PCI_BAR_COUNT ||
      rebar + PCI_REBAR_CAPABILITY + entries * PCI_REBAR_ENTRY_BYTES > PCI_CONFIG_BYTES) {
    return fail(proof, "invalid Resizable BAR entry count");
  }
  for (size_t i = 0; i < count; ++i) {
    if (headers[i] > rebar &&
        headers[i] < rebar + PCI_REBAR_CAPABILITY + entries * PCI_REBAR_ENTRY_BYTES) {
      return fail(proof, "overlapping Resizable BAR metadata");
    }
  }
  for (unsigned i = 0; i < entries; ++i) {
    unsigned offset = rebar + i * PCI_REBAR_ENTRY_BYTES;
    uint32_t control = pci_read32(device->address, offset + PCI_REBAR_CONTROL);
    uint32_t supported = pci_read32(device->address, offset + PCI_REBAR_CAPABILITY);
    unsigned bar = control & PCI_REBAR_BAR_MASK;
    unsigned size = (control >> PCI_REBAR_SIZE_SHIFT) & PCI_REBAR_SIZE_MASK;
    if (control == UINT32_MAX || supported == UINT32_MAX ||
        bar >= PCI_BAR_COUNT || !bars[bar].memory || bars[bar].upper || bars[bar].bytes ||
        size > PCI_REBAR_MAX_SIZE_CODE || !(supported & (1u << (size + PCI_REBAR_SUPPORT_SHIFT)))) {
      return fail(proof, "invalid Resizable BAR current-size metadata");
    }
    uint64_t bytes = UINT64_C(1) << (PCI_REBAR_SIZE_BASE_SHIFT + size);
    if ((bars[bar].base & (bytes - 1)) || bytes - 1 > UINT64_MAX - bars[bar].base ||
        (!bars[bar].memory64 && bytes > BELOW_4G - bars[bar].base)) {
      return fail(proof, "Resizable BAR size/base conflict");
    }
    bars[bar].bytes = bytes;
  }
  return true;
}

static bool device_exclusions(struct proof *proof, const struct pci_device *device)
{
  struct pci_address address = device->address;
  if (pci_read16(address, PCI_VENDOR_ID) != device->vendor_id ||
      pci_read16(address, PCI_DEVICE_ID) != device->device_id ||
      (pci_read8(address, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) != device->header_type) {
    return fail(proof, "PCI identity changed since inventory");
  }
  if (device->header_type != PCI_HEADER_ENDPOINT && device->header_type != PCI_HEADER_BRIDGE) {
    return fail(proof, "unsupported PCI header");
  }
  bool active, express;
  struct decoded_bar bars[PCI_BAR_COUNT] = {0};
  if (!decode_active(proof, device, &active, &express) || !decode_bars(proof, device, bars) ||
      (express && !extended_sizes(proof, device, bars))) {
    return false;
  }
  for (unsigned i = 0; i < PCI_BAR_COUNT; ++i) {
    struct decoded_bar *bar = &bars[i];
    if (!bar->memory || bar->upper) {
      continue;
    }
    if (device->owner && device->owner->bars[i].bytes) {
      const struct pci_bar *known = &device->owner->bars[i];
      if (known->physical != bar->base || (bar->bytes && known->bytes != bar->bytes)) {
        return fail(proof, "owned BAR differs from hardware");
      }
      bar->bytes = known->bytes;
    }
    if (bar->base) {
      uint64_t bytes = bar->bytes ? bar->bytes : bar->base & (0 - bar->base);
      if ((!bar->memory64 && bytes > BELOW_4G - bar->base) ||
          !exclude_size(proof, bar->base, bytes)) {
        return fail(proof, "foreign BAR bound overflow");
      }
    } else if (active) {
      /* Raw zero may be an unimplemented slot or a live zero-base decoder.
       * Without a write probe, exclude the largest size its address bits can
       * encode: an implemented BAR keeps at least its top address bit writable. */
      uint64_t bytes = bar->bytes ? bar->bytes :
          (bar->memory64 ? PCI_BAR64_MAX_BYTES : PCI_BAR32_MAX_BYTES);
      if (!exclude_size(proof, 0, bytes)) {
        return false;
      }
    }
  }
  uint32_t rom = pci_read32(address,
      device->header_type == PCI_HEADER_ENDPOINT ? PCI_ROM_ENDPOINT : PCI_ROM_BRIDGE);
  if (rom == UINT32_MAX) {
    return fail(proof, "inaccessible expansion ROM register");
  }
  uint64_t base = rom & PCI_ROM_ADDRESS_MASK;
  if (base) {
    if (!exclude_size(proof, base, base & (0 - base))) {
      return false;
    }
  } else if (rom & PCI_ROM_ENABLE) {
    if (!exclude_size(proof, 0, PCI_ROM_MAX_BYTES)) {
      return false;
    }
  }
  if (device->header_type == PCI_HEADER_BRIDGE) {
    if (on_path(proof, device) && !active) {
      return fail(proof, "ancestor bridge memory forwarding is inactive");
    }
    if (device->interface != 0) {
      return fail(proof, "subtractive or unsupported PCI bridge");
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
      struct interval range;
      bool enabled;
      if (!bridge_window(proof, device, kind != 0, &range, &enabled)) {
        return false;
      }
      if (enabled && !on_path(proof, device) && !exclude(proof, range.first, range.last)) {
        return false;
      }
    }
  }
  return true;
}

static bool make_proof(struct proof *proof, const struct acpi_pci_resources *resources,
    const struct pci_device *device)
{
  if (pci_inventory_state() != PCI_INVENTORY_COMPLETE || !find_route(proof, resources, device)) {
    return fail(proof, proof->failure ? proof->failure : "incomplete PCI inventory");
  }
  size_t fixed = resources->reservation_count + resources->window_count +
      memory_region_count + efi_region_count + 1;
  size_t attempts = 0;
  for (struct allocation *a = allocations; a; a = a->next) {
    ++attempts;
  }
  size_t maximum = SIZE_MAX / sizeof(struct interval);
  if (fixed > maximum || attempts > maximum - fixed ||
      pci_device_count() > (maximum - fixed - attempts) / 10) {
    return fail(proof, "exclusion allocation overflow");
  }
  proof->capacity = fixed + attempts + pci_device_count() * 10;
  proof->excluded = kmalloc(proof->capacity * sizeof(*proof->excluded));
  if (!proof->excluded) {
    return fail(proof, "exclusion allocation failed");
  }
  for (size_t i = 0; i < memory_region_count; ++i) {
    if (!exclude_size(proof, memory_regions[i].base, memory_regions[i].length)) {
      return false;
    }
  }
  for (size_t i = 0; i < efi_region_count; ++i) {
    if (!exclude_size(proof, efi_regions[i].base, efi_regions[i].length)) {
      return false;
    }
  }
  for (size_t i = 0; i < resources->reservation_count; ++i) {
    const struct acpi_pci_resource_range *r = &resources->reservations[i].range;
    if (!exclude(proof, r->first, r->last)) {
      return false;
    }
  }
  for (size_t i = 0; i < resources->window_count; ++i) {
    const struct acpi_pci_window *w = &resources->windows[i];
    if (w->root != proof->root && !exclude(proof, w->range.first, w->range.last)) {
      return false;
    }
  }
  /* PCI legacy VGA/option-ROM decode is independent of ordinary BARs. */
  if (!exclude(proof, UINT64_C(0xa0000), UINT64_C(0xfffff))) {
    return false;
  }
  for (struct allocation *a = allocations; a; a = a->next) {
    if (a->reserved && !exclude(proof, a->extent.first, a->extent.last)) {
      return false;
    }
  }
  for (size_t i = 0; i < pci_device_count(); ++i) {
    if (!device_exclusions(proof, pci_device_at(i))) {
      return false;
    }
  }
  kdebug("PCI: allocation proof: %zu functions, %zu exclusions; boot=%zu EFI=%zu ACPI=%zu\n",
      pci_device_count(), proof->count, memory_region_count, efi_region_count,
      resources->reservation_count);
  return true;
}

static bool gap(struct proof *proof, struct interval range, uint64_t bytes,
    uint64_t alignment, uint64_t *base)
{
  uint64_t first = range.first;
  if (!first) {
    first = PAGE_SIZE;
  }
  for (;;) {
    if (first > UINT64_MAX - (alignment - 1)) {
      return false;
    }
    first = (first + alignment - 1) & ~(alignment - 1);
    if (first > range.last || bytes - 1 > range.last - first) {
      return false;
    }
    uint64_t last = first + bytes - 1;
    bool collision = false;
    uint64_t occupied_last = first;
    for (size_t i = 0; i < proof->count; ++i) {
      struct interval r = proof->excluded[i];
      if (r.first <= last && first <= r.last) {
        collision = true;
        if (r.last > occupied_last) {
          occupied_last = r.last;
        }
      }
    }
    if (!collision) {
      *base = first;
      return true;
    }
    if (occupied_last == UINT64_MAX) {
      return false;
    }
    first = occupied_last + 1;
  }
}

static bool choose_extent(struct proof *proof, const struct acpi_pci_resources *resources,
    const struct pci_bar_placement *placement, uint64_t bytes, struct interval *extent)
{
  uint64_t alignment = placement->bytes > PAGE_SIZE ? placement->bytes : PAGE_SIZE;
  unsigned passes = placement->memory64 ? 2 : 1;
  unsigned checks = 0;
  if (bytes > SIZE_MAX) {
    return fail(proof, "BAR extent exceeds native address size");
  }
  for (unsigned pass = 0; pass < passes; ++pass) {
    bool above = placement->memory64 && pass == 0;
    for (size_t i = 0; i < resources->window_count; ++i) {
      const struct acpi_pci_window *w = &resources->windows[i];
      if (w->root != proof->root || !w->writable || (w->prefetchable && !placement->prefetchable)) {
        continue;
      }
      for (unsigned kind = 0; kind < (placement->prefetchable ? 2u : 1u); ++kind) {
        struct interval range = {w->range.first, w->range.last};
        bool usable = true;
        for (size_t n = 0; n < proof->depth; ++n) {
          struct interval forwarding;
          bool enabled;
          if (!bridge_window(proof, proof->path[n], kind != 0, &forwarding, &enabled)) {
            return false;
          }
          if (!enabled) {
            usable = false;
            break;
          }
          if (forwarding.first > range.first) {
            range.first = forwarding.first;
          }
          if (forwarding.last < range.last) {
            range.last = forwarding.last;
          }
        }
        if (above) {
          if (range.first < BELOW_4G) {
            range.first = BELOW_4G;
          }
        } else if (range.last >= BELOW_4G) {
          range.last = BELOW_4G - 1;
        }
        if (!usable || range.first > range.last) {
          continue;
        }
        uint64_t base;
        while (gap(proof, range, bytes, alignment, &base)) {
          if (checks == PCI_PLACEMENT_CHECK_LIMIT) {
            return fail(proof, "architecture/VM placement candidate budget exhausted");
          }
          ++checks;
          if (arch_pci_mmio_available(base, bytes) && !vm_kernel_physical_overlap(base, bytes)) {
            *extent = (struct interval){base, base + bytes - 1};
            return true;
          }
          /* A CPU-reserved or mapped alias is not necessarily in firmware's
           * exclusions. Keep searching this aperture, without unbounded scans. */
          if (base > UINT64_MAX - alignment) {
            break;
          }
          range.first = base + alignment;
        }
      }
    }
  }
  return fail(proof, "no proven aligned gap in eligible root/bridge windows");
}

bool pci_assign_memory_bar(struct pci_claim *claim, unsigned bar,
    struct pci_assignment_window *window, uintptr_t *address)
{
  KASSERT(!cpu_interrupts_enabled());
  KASSERT(claim && claim->device && claim->device->owner == claim && address);
  *address = 0;
  struct proof proof = {0};
  const struct acpi_pci_resources *resources = NULL;
  struct allocation *allocation = NULL;
  struct pci_bar_placement placement;
  if (placement_uncertain) {
    fail(&proof, "earlier BAR write left an unbounded uncertain decode");
    goto refused;
  }
  if (!prepared || !firmware_map_complete) {
    fail(&proof, "boot/EFI memory map incomplete");
    goto refused;
  }
  if (!window || !window->base || window->consumed ||
      window->claim != claim || window->generation != claim->generation) {
    fail(&proof, "native window missing, consumed or stale");
    goto refused;
  }
  if (acpi_pci_resources_snapshot(&resources) != ACPI_PCI_RESOURCES_READY) {
    fail(&proof, "native window or firmware resource capture unavailable");
    goto refused;
  }
  for (struct allocation *a = allocations; a; a = a->next) {
    if (a->device == claim->device && a->bar == bar) {
      fail(&proof, "BAR assignment already attempted");
      goto refused;
    }
  }
  allocation = kmalloc(sizeof(*allocation));
  if (!allocation) {
    fail(&proof, "assignment record allocation failed");
    goto refused;
  }
  *allocation = (struct allocation){.next = allocations, .device = claim->device,
      .generation = claim->generation, .bar = bar};
  allocations = allocation;
  enum pci_bar_placement_result result = pci_probe_unassigned_memory_bar(claim, bar, &placement);
  if (result != PCI_BAR_PLACEMENT_OK) {
    placement_uncertain |= result == PCI_BAR_PLACEMENT_UNCERTAIN;
    fail(&proof, pci_bar_placement_result_name(result));
    goto refused;
  }
  if (placement.bytes > UINT64_MAX - (PAGE_SIZE - 1)) {
    fail(&proof, "sized BAR does not fit native mapping extent");
    goto refused;
  }
  uint64_t bytes = (placement.bytes + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
  if (window->bytes > bytes) {
    fail(&proof, "native mapping exceeds the rounded BAR extent");
    goto refused;
  }
  if (!make_proof(&proof, resources, claim->device) ||
      !choose_extent(&proof, resources, &placement, bytes, &allocation->extent)) {
    goto refused;
  }
  /* Reserve before either a mapping or device register can name the range. */
  allocation->reserved = true;
  window->consumed = true;
  claim->assignment_pinned = true;
  for (size_t offset = 0; offset < window->bytes; offset += PAGE_SIZE) {
    if (vm_map_mmio(window->base + offset, allocation->extent.first + offset) != MM_OK) {
      fail(&proof, "fresh native mapping failed; range retained");
      goto refused;
    }
  }
  result = pci_program_unassigned_memory_bar(claim, &placement, allocation->extent.first);
  if (result != PCI_BAR_PLACEMENT_OK) {
    placement_uncertain |= result == PCI_BAR_PLACEMENT_UNCERTAIN;
    fail(&proof, pci_bar_placement_result_name(result));
    goto refused;
  }
  claim->bars[bar] = (struct pci_bar){.physical = allocation->extent.first, .bytes = placement.bytes};
  *address = window->base;
  struct pci_address a = claim->device->address;
  kinfo("PCI: %s %02x:%02x.%x BAR%u assigned bus=0x%llx physical=0x%llx bytes=0x%llx; "
      "decode/BME off\n", resources->roots[proof.root].path, a.bus, a.device, a.function,
      bar, (unsigned long long)allocation->extent.first, (unsigned long long)allocation->extent.first,
      (unsigned long long)placement.bytes);
  kfree(proof.excluded);
  return true;

refused:
  kinfo("PCI: %02x:%02x.%x BAR%u assignment refused: %s\n", claim->device->address.bus,
      claim->device->address.device, claim->device->address.function, bar,
      proof.failure ? proof.failure : "unknown resource failure");
  kfree(proof.excluded);
  return false;
}
