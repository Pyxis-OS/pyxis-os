#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/pci.h>
#include <kernel/pci/registers.h>

static struct pci_device *devices;
static size_t device_count;
static bool inventory_available;
static bool inventory_complete;

struct pci_bus {
  uint8_t number, last_bus, parent;
};

struct pci_scan {
  /* These bounds come from PCI's eight-bit bus numbers, not a device limit. */
  struct pci_bus pending[PCI_BUS_COUNT];
  bool seen[PCI_BUS_COUNT];
  unsigned bus_count, functions;
  bool incomplete;
};

static void report_problem(struct pci_scan *scan, struct pci_address address,
                            const char *reason)
{
  klog("PCI 0:%x:%x.%u: %s; inventory incomplete\n",
       address.bus, address.device, address.function, reason);
  scan->incomplete = true;
}

static void report_bars(struct pci_scan *scan, struct pci_address address,
                         unsigned count)
{
  for (unsigned index = 0; index < count; ++index) {
    uint32_t low = pci_read32(address, PCI_BAR_FIRST + index * PCI_REGISTER_BYTES);
    if (!low) {
      /* A zero BAR cannot distinguish absent storage from an unassigned BAR
       * without a write probe. Discovery deliberately does not size resources. */
      continue;
    }

    if (low & PCI_BAR_IO) {
      klog("  BAR%u: I/O base=0x%x\n", index, low & PCI_BAR_IO_ADDRESS_MASK);
      continue;
    }

    unsigned type = low & PCI_BAR_MEMORY_TYPE_MASK;
    uint64_t base = low & PCI_BAR_MEMORY_ADDRESS_MASK;
    unsigned first = index;
    if (type == PCI_BAR_MEMORY_64) {
      if (index + 1 == count) {
        report_problem(scan, address, "64-bit BAR lacks its upper register");
        return;
      }
      ++index;
      base |= (uint64_t)pci_read32(address,
        PCI_BAR_FIRST + index * PCI_REGISTER_BYTES) << PCI_BAR_HIGH_SHIFT;
    } else if (type != PCI_BAR_MEMORY_32) {
      report_problem(scan, address, "unsupported memory BAR type");
      continue;
    }

    klog("  BAR%u: memory%u base=0x%lx prefetchable=%u\n", first,
         type == PCI_BAR_MEMORY_64 ? 64u : 32u, base,
         (unsigned)((low & PCI_BAR_PREFETCHABLE) != 0));
  }
}

static const char *capability_name(unsigned id)
{
  switch (id) {
  case PCI_CAP_POWER: return "power management";
  case PCI_CAP_MSI: return "MSI";
  case PCI_CAP_VENDOR: return "vendor-specific";
  case PCI_CAP_EXPRESS: return "PCI Express";
  case PCI_CAP_MSIX: return "MSI-X";
  default: return "other";
  }
}

static void report_extended_capabilities(struct pci_scan *scan,
                                          struct pci_address address)
{
  bool seen[PCI_CONFIG_BYTES / PCI_REGISTER_BYTES] = {0};
  unsigned offset = PCI_EXT_CAP_FIRST;
  while (offset) {
    if (offset < PCI_EXT_CAP_FIRST || offset > PCI_CONFIG_BYTES - PCI_REGISTER_BYTES ||
        offset % PCI_REGISTER_BYTES || seen[offset / PCI_REGISTER_BYTES]) {
      report_problem(scan, address, "invalid extended capability chain");
      return;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;

    uint32_t header = pci_read32(address, offset);
    if (!header || header == UINT32_MAX) {
      return;
    }
    klog("  extended capability 0x%x version=%u at 0x%x\n",
         header & PCI_EXT_CAP_ID_MASK,
         (header >> PCI_EXT_CAP_VERSION_SHIFT) & PCI_EXT_CAP_VERSION_MASK, offset);
    offset = header >> PCI_EXT_CAP_NEXT_SHIFT;
  }
}

static void report_capabilities(struct pci_scan *scan, struct pci_address address)
{
  if (!(pci_read16(address, PCI_STATUS) & PCI_STATUS_CAPABILITIES)) {
    return;
  }

  bool seen[PCI_CONVENTIONAL_BYTES / PCI_REGISTER_BYTES] = {0};
  bool express = false;
  unsigned offset = pci_read8(address, PCI_CAPABILITIES) & PCI_CAP_POINTER_MASK;
  while (offset) {
    if (offset < PCI_CAP_FIRST || seen[offset / PCI_REGISTER_BYTES]) {
      report_problem(scan, address, "invalid capability chain");
      return;
    }
    seen[offset / PCI_REGISTER_BYTES] = true;

    unsigned id = pci_read8(address, offset + PCI_CAP_ID);
    klog("  capability 0x%x (%s) at 0x%x\n", id, capability_name(id), offset);
    express |= id == PCI_CAP_EXPRESS;
    offset = pci_read8(address, offset + PCI_CAP_NEXT) & PCI_CAP_POINTER_MASK;
  }

  if (express) {
    report_extended_capabilities(scan, address);
  }
}

static void discover_bridge(struct pci_scan *scan, struct pci_address address,
                              unsigned parent_limit)
{
  unsigned primary = pci_read8(address, PCI_BRIDGE_PRIMARY);
  unsigned secondary = pci_read8(address, PCI_BRIDGE_SECONDARY);
  unsigned subordinate = pci_read8(address, PCI_BRIDGE_SUBORDINATE);
  klog("  bridge: primary=%u secondary=%u subordinate=%u\n",
       primary, secondary, subordinate);

  if (!secondary && !subordinate) {
    klog("  bridge has no configured downstream bus\n");
    return;
  }
  if (primary != address.bus || secondary <= primary || secondary > subordinate ||
      subordinate > parent_limit || subordinate >= arch_pci_bus_count() ||
      scan->seen[secondary]) {
    report_problem(scan, address, "unsupported or overlapping bridge bus assignment");
    return;
  }

  for (unsigned i = 1; i < scan->bus_count; ++i) {
    const struct pci_bus *sibling = &scan->pending[i];
    if (sibling->parent == address.bus && secondary <= sibling->last_bus &&
        sibling->number <= subordinate) {
      report_problem(scan, address, "overlapping sibling bridge bus ranges");
      return;
    }
  }

  scan->seen[secondary] = true;
  scan->pending[scan->bus_count++] = (struct pci_bus){secondary, subordinate, primary};
}

static void discover_function(struct pci_scan *scan, struct pci_address address,
                               unsigned parent_limit)
{
  unsigned vendor = pci_read16(address, PCI_VENDOR_ID);
  if (vendor == PCI_NO_VENDOR) {
    return;
  }

  unsigned device = pci_read16(address, PCI_DEVICE_ID);
  unsigned class = pci_read8(address, PCI_CLASS);
  unsigned subclass = pci_read8(address, PCI_SUBCLASS);
  unsigned interface = pci_read8(address, PCI_INTERFACE);
  unsigned revision = pci_read8(address, PCI_REVISION);
  unsigned header = pci_read8(address, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK;
  klog("PCI 0:%x:%x.%u: vendor=0x%x device=0x%x class=%x:%x:%x revision=0x%x header=%u\n",
       address.bus, address.device, address.function, vendor, device,
       class, subclass, interface, revision, header);
  ++scan->functions;

  struct pci_device *record = kmalloc(sizeof(*record));
  if (!record) {
    report_problem(scan, address, "cannot retain device for driver lookup");
  } else {
    *record = (struct pci_device){
      .address = address, .vendor_id = vendor, .device_id = device,
      .base_class = class, .subclass = subclass, .interface = interface,
      .revision = revision, .header_type = header, .next = devices,
    };
    devices = record;
    ++device_count;
  }

  if (header != PCI_HEADER_ENDPOINT && header != PCI_HEADER_BRIDGE) {
    report_problem(scan, address, "unsupported header type");
    return;
  }
  report_bars(scan, address, header == PCI_HEADER_ENDPOINT ?
              PCI_ENDPOINT_BARS : PCI_BRIDGE_BARS);
  report_capabilities(scan, address);

  if (header == PCI_HEADER_BRIDGE) {
    if (class != PCI_CLASS_BRIDGE || subclass != PCI_SUBCLASS_PCI_BRIDGE) {
      report_problem(scan, address, "bridge header/class mismatch");
      return;
    }
    discover_bridge(scan, address, parent_limit);
  }
}

void pci_discover(void)
{
  inventory_complete = false;
  unsigned buses = arch_pci_bus_count();
  inventory_available = buses != 0;
  if (!buses) {
    return;
  }

  struct pci_scan scan = {
    .pending = {{.number = 0, .last_bus = buses - 1}},
    .seen = {true},
    .bus_count = 1,
  };
  for (unsigned next = 0; next < scan.bus_count; ++next) {
    struct pci_bus bus = scan.pending[next];
    for (unsigned device = 0; device < PCI_DEVICE_COUNT; ++device) {
      struct pci_address address = {bus.number, device, 0};
      if (pci_read16(address, PCI_VENDOR_ID) == PCI_NO_VENDOR) {
        continue;
      }

      unsigned header = pci_read8(address, PCI_HEADER_TYPE);
      unsigned functions = header & PCI_HEADER_MULTIFUNCTION ? PCI_FUNCTION_COUNT : 1;
      for (unsigned function = 0; function < functions; ++function) {
        address.function = function;
        discover_function(&scan, address, bus.last_bus);
      }
    }
  }
  klog("PCI: %u functions on %u buses; read-only inventory%s\n",
       scan.functions, scan.bus_count, scan.incomplete ? " incomplete" : " complete");
  inventory_complete = !scan.incomplete;
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device)
{
  struct pci_device *first = NULL;
  for (struct pci_device *entry = devices; entry; entry = entry->next) {
    if (entry->vendor_id == vendor && entry->device_id == device) {
      first = entry;
    }
  }
  return first;
}

enum pci_selection pci_select_device(uint16_t vendor, uint16_t device,
    struct pci_device **selected)
{
  *selected = NULL;
  if (!inventory_complete) {
    return PCI_SELECTION_INCOMPLETE;
  }
  struct pci_device *match = NULL;
  for (struct pci_device *entry = devices; entry; entry = entry->next) {
    if (entry->vendor_id == vendor && entry->device_id == device) {
      if (match) {
        return PCI_SELECTION_AMBIGUOUS;
      }
      match = entry;
    }
  }
  *selected = match;
  return match ? PCI_SELECTION_UNIQUE : PCI_SELECTION_ABSENT;
}

enum pci_selection pci_select_class(uint8_t base_class, uint8_t subclass,
    uint8_t interface, struct pci_device **selected)
{
  *selected = NULL;
  if (!inventory_complete) {
    return PCI_SELECTION_INCOMPLETE;
  }
  struct pci_device *match = NULL;
  for (struct pci_device *entry = devices; entry; entry = entry->next) {
    if (entry->base_class == base_class && entry->subclass == subclass &&
        entry->interface == interface) {
      if (match) {
        return PCI_SELECTION_AMBIGUOUS;
      }
      match = entry;
    }
  }
  *selected = match;
  return match ? PCI_SELECTION_UNIQUE : PCI_SELECTION_ABSENT;
}

enum pci_inventory_state pci_inventory_state(void)
{
  if (!inventory_available) {
    return PCI_INVENTORY_UNAVAILABLE;
  }
  return inventory_complete ? PCI_INVENTORY_COMPLETE : PCI_INVENTORY_INCOMPLETE;
}

size_t pci_device_count(void)
{
  return device_count;
}

const struct pci_device *pci_device_at(size_t index)
{
  const struct pci_device *entry = devices;
  for (; entry && index; --index) {
    entry = entry->next;
  }
  return entry;
}

bool pci_reserve_device_at(size_t index, struct pci_claim *claim)
{
  struct pci_device *entry = devices;
  for (; entry && index; --index) {
    entry = entry->next;
  }
  return pci_reserve_device(entry, claim);
}
