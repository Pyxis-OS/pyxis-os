#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/virtio/transport.h>

#define VIRTIO_CAP_LENGTH 2
#define VIRTIO_CAP_TYPE 3
#define VIRTIO_CAP_BAR 4
#define VIRTIO_CAP_OFFSET 8
#define VIRTIO_CAP_REGION_LENGTH 12
#define VIRTIO_CAP_BYTES 16
#define VIRTIO_NOTIFY_CAP_BYTES 20
#define VIRTIO_NOTIFY_MULTIPLIER 16
#define VIRTIO_COMMON_CONFIG 1
#define VIRTIO_NOTIFY_CONFIG 2
#define VIRTIO_ISR_CONFIG 3
#define VIRTIO_DEVICE_CONFIG 4
#define VIRTIO_PCI_CONFIG 5
#define VIRTIO_PCI_CONFIG_BYTES 20
#define VIRTIO_PCI_CONFIG_DATA 16
#define VIRTIO_NOTIFY_BYTES 2
#define VIRTIO_ISR_BYTES 1

#define VIRTIO_COMMON_STATUS offsetof(struct virtio_pci_common, device_status)
#define VIRTIO_MSIX_ENTRY 0

#define transport_log(pci, ...) do { \
  if ((pci)->trace_details) { \
    ktrace(__VA_ARGS__); \
  } else { \
    klog(__VA_ARGS__); \
  } \
} while (0)

bool virtio_pci_disable_msix(struct virtio_pci_transport *pci)
{
  return pci_msix_disable(&pci->msix);
}

bool virtio_pci_prepare_msix(struct virtio_pci_transport *pci, uint8_t vector)
{
  /* VirtIO accepts vector indices only with MSI-X enabled. Shared setup leaves
   * the function and every entry masked through the driver-specific writes. */
  if (!pci_msix_prepare(&pci->msix, vector)) {
    return false;
  }

  /* These registers contain table indices, not CPU interrupt vectors. A
   * rejected mapping reads back as NO_VECTOR (0xffff), even for a valid index. */
  volatile struct virtio_pci_common *common = virtio_pci_common(pci);
  common->config_msix_vector = VIRTIO_MSIX_ENTRY;
  if (common->config_msix_vector != VIRTIO_MSIX_ENTRY) {
    return false;
  }
  transport_log(pci, "%s PCI: MSI-X entry %u -> BSP APIC %u vector %u; route masked\n",
       pci->name, VIRTIO_MSIX_ENTRY, cpu_bsp()->lapic_id, (unsigned)vector);
  return true;
}

bool virtio_pci_reset(struct virtio_pci_transport *pci)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(pci);
  common->device_status = 0;
  uint64_t start = arch_monotonic_ns();
  do {
    if (common->device_status == 0) {
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < VIRTIO_RESET_TIMEOUT_NS);
  return false;
}

bool virtio_pci_inspect_queue(struct virtio_pci_transport *pci, unsigned index,
    struct virtio_queue_info *queue)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(pci);
  common->queue_select = index;
  uint16_t size = common->queue_size;
  if (!size || (size & (size - 1)) || common->queue_enable) {
    return false;
  }

  /* queue_notify_off is in multiplier units, not bytes. Widen before multiplying. */
  uint64_t offset = (uint64_t)common->queue_notify_off * pci->notify_multiplier;
  if (offset > pci->notify.length ||
      VIRTIO_NOTIFY_BYTES > pci->notify.length - offset) {
    return false;
  }
  *queue = (struct virtio_queue_info){
    .max_size = size,
    .notify_address = pci->notify.mapping.address + offset,
  };
  transport_log(pci, "%s PCI: queue %u maximum=%u notify=%p, disabled\n", pci->name,
       index, (unsigned)size, (void *)queue->notify_address);
  return true;
}

/* A capability must name a BAR register, not the high half of a 64-bit BAR. */
static bool assigned_memory_bar(struct pci_address address, unsigned wanted)
{
  for (unsigned bar = 0; bar < PCI_BAR_COUNT; ++bar) {
    unsigned offset = PCI_BAR_FIRST + bar * PCI_REGISTER_BYTES;
    uint32_t low = pci_read32(address, offset);
    bool wide = !(low & PCI_BAR_IO) &&
      (low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_64;
    if (bar == wanted) {
      if (low & PCI_BAR_IO) {
        return false;
      }
      if (wide && bar + 1 < PCI_BAR_COUNT) {
        return (low & PCI_BAR_MEMORY_ADDRESS_MASK) ||
          pci_read32(address, offset + PCI_REGISTER_BYTES);
      }
      return (low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_32 &&
        (low & PCI_BAR_MEMORY_ADDRESS_MASK);
    }
    if (wide) {
      ++bar;
    }
  }
  return false;
}

/* PCI configuration access reaches status before we can safely probe BAR sizes.
 * The advertised common capability bounds this byte; BAR bounds follow reset. */
static bool reset_before_probe(struct virtio_pci_transport *pci)
{
  struct pci_claim *claim = &pci->claim;
  struct pci_address address = claim->device->address;
  unsigned common = 0, access = 0;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(address, offset) != PCI_CAP_VENDOR) {
      continue;
    }
    if (!pci_capability_fits(claim, offset, 4)) {
      return false;
    }
    unsigned type = pci_read8(address, offset + VIRTIO_CAP_TYPE);
    if ((type == VIRTIO_COMMON_CONFIG && !common) ||
        (type == VIRTIO_PCI_CONFIG && !access)) {
      unsigned length = pci_read8(address, offset + VIRTIO_CAP_LENGTH);
      unsigned required = type == VIRTIO_COMMON_CONFIG ? VIRTIO_CAP_BYTES : VIRTIO_PCI_CONFIG_BYTES;
      if (length < required || !pci_capability_fits(claim, offset, length)) {
        return false;
      }
      if (type == VIRTIO_COMMON_CONFIG) {
        if (pci_read8(address, offset + VIRTIO_CAP_BAR) < PCI_BAR_COUNT) {
          common = offset;
        }
      } else {
        access = offset;
      }
    }
  }
  if (!common || !access) {
    return false;
  }
  unsigned bar = pci_read8(address, common + VIRTIO_CAP_BAR);
  uint32_t offset = pci_read32(address, common + VIRTIO_CAP_OFFSET);
  uint32_t length = pci_read32(address, common + VIRTIO_CAP_REGION_LENGTH);
  if (length < VIRTIO_COMMON_BYTES || offset % 4 ||
      offset > UINT32_MAX - VIRTIO_COMMON_STATUS || !assigned_memory_bar(address, bar)) {
    return false;
  }

  uint8_t saved_bar = pci_read8(address, access + VIRTIO_CAP_BAR);
  uint32_t saved_offset = pci_read32(address, access + VIRTIO_CAP_OFFSET);
  uint32_t saved_length = pci_read32(address, access + VIRTIO_CAP_REGION_LENGTH);
  pci_write8(claim, access + VIRTIO_CAP_BAR, bar);
  pci_write32(claim, access + VIRTIO_CAP_OFFSET, offset + VIRTIO_COMMON_STATUS);
  pci_write32(claim, access + VIRTIO_CAP_REGION_LENGTH, 1);
  pci_write8(claim, access + VIRTIO_PCI_CONFIG_DATA, 0);

  uint64_t start = arch_monotonic_ns();
  bool stopped = false;
  do {
    stopped = pci_read8(address, access + VIRTIO_PCI_CONFIG_DATA) == 0;
    if (stopped) {
      break;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < VIRTIO_RESET_TIMEOUT_NS);

  pci_write8(claim, access + VIRTIO_CAP_BAR, saved_bar);
  pci_write32(claim, access + VIRTIO_CAP_OFFSET, saved_offset);
  pci_write32(claim, access + VIRTIO_CAP_REGION_LENGTH, saved_length);
  if (stopped) {
    transport_log(pci, "%s PCI: reset confirmed before BAR probing\n", pci->name);
  }
  return stopped;
}

static bool region_fits(const struct virtio_pci_transport *pci,
                        const struct pci_region *region)
{
  return region->bar < PCI_BAR_COUNT && region->length &&
    region->offset <= pci->claim.bars[region->bar].bytes &&
    region->length <= pci->claim.bars[region->bar].bytes - region->offset;
}

static bool read_virtio_capability(struct virtio_pci_transport *pci, unsigned offset,
                                   size_t device_bytes, unsigned device_alignment)
{
  struct pci_claim *claim = &pci->claim;
  struct pci_address address = claim->device->address;
  if (!pci_capability_fits(claim, offset, VIRTIO_CAP_BAR)) {
    return false;
  }
  unsigned type = pci_read8(address, offset + VIRTIO_CAP_TYPE);
  struct pci_region *region;
  size_t minimum;
  unsigned alignment;
  switch (type) {
  case VIRTIO_COMMON_CONFIG:
    region = &pci->common;
    minimum = VIRTIO_COMMON_BYTES;
    alignment = 4;
    break;
  case VIRTIO_NOTIFY_CONFIG:
    region = &pci->notify;
    minimum = VIRTIO_NOTIFY_BYTES;
    alignment = 2;
    break;
  case VIRTIO_ISR_CONFIG:
    region = &pci->isr;
    minimum = VIRTIO_ISR_BYTES;
    alignment = 1;
    break;
  case VIRTIO_DEVICE_CONFIG:
    if (!device_bytes) {
      return true; /* This device type has no device-specific configuration. */
    }
    region = &pci->device;
    minimum = device_bytes;
    alignment = device_alignment;
    break;
  default:
    return true; /* Optional/unknown structures are not used by this driver. */
  }
  if (region->length) {
    return true; /* Use the first supported instance, as ordered by the device. */
  }

  unsigned length = pci_read8(address, offset + VIRTIO_CAP_LENGTH);
  unsigned required = type == VIRTIO_NOTIFY_CONFIG ? VIRTIO_NOTIFY_CAP_BYTES : VIRTIO_CAP_BYTES;
  if (length < required || !pci_capability_fits(claim, offset, length)) {
    return false;
  }
  unsigned bar = pci_read8(address, offset + VIRTIO_CAP_BAR);
  if (bar >= PCI_BAR_COUNT) {
    return true; /* Reserved BAR values must be ignored. */
  }
  *region = (struct pci_region){
    .bar = bar,
    .offset = pci_read32(address, offset + VIRTIO_CAP_OFFSET),
    .length = pci_read32(address, offset + VIRTIO_CAP_REGION_LENGTH),
  };
  if (!region_fits(pci, region) || region->length < minimum || region->offset % alignment) {
    return false;
  }
  if (type == VIRTIO_NOTIFY_CONFIG) {
    pci->notify_multiplier = pci_read32(address, offset + VIRTIO_NOTIFY_MULTIPLIER);
    if (pci->notify_multiplier % alignment) {
      return false;
    }
  }
  return true;
}

static bool regions_disjoint(const struct virtio_pci_transport *pci, bool device_config)
{
  const struct pci_region *regions[] = {
    &pci->common, &pci->notify, &pci->isr, &pci->device,
    &pci->msix.table, &pci->msix.pba,
  };
  for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
    const struct pci_region *a = regions[i];
    if (a == &pci->device && !device_config) {
      continue;
    }
    if (!region_fits(pci, a)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      const struct pci_region *b = regions[j];
      if (!b->length) {
        continue;
      }
      if (a->bar == b->bar && a->offset < (uint64_t)b->offset + b->length &&
          b->offset < (uint64_t)a->offset + a->length) {
        return false;
      }
    }
  }
  return true;
}

static bool map_region(struct virtio_pci_transport *pci, const char *name,
                       struct pci_region *region, size_t needed,
                       const struct boot_info *boot)
{
  if (!region_fits(pci, region)) {
    return false;
  }
  enum mm_result result = pci_map_bar(&pci->claim, region->bar, region->offset,
                                      needed, boot, &region->mapping);
  if (result != MM_OK) {
    transport_log(pci, "%s PCI: cannot map %s (error %u)\n", pci->name, name, (unsigned)result);
    return false;
  }
  transport_log(pci, "%s PCI: %s BAR%u offset=0x%x bytes=0x%zx address=%p\n", pci->name,
       name, region->bar, region->offset, needed, (void *)region->mapping.address);
  return true;
}

bool virtio_pci_prepare(struct virtio_pci_transport *pci, struct pci_device *device,
    const struct boot_info *boot, size_t device_bytes, unsigned device_alignment)
{
  struct pci_claim *claim = &pci->claim;
  if (!pci_claim_device(device, claim)) {
    if (claim->device) {
      transport_log(pci, "%s PCI: cannot confirm DMA/INTx disable; claim retained until reboot\n", pci->name);
    } else {
      transport_log(pci, "%s PCI: function busy or unsupported; resources not claimed\n", pci->name);
    }
    return false;
  }
  const char *failure = "cannot confirm initial reset";
  if (!reset_before_probe(pci)) {
    goto fail;
  }
  pci_write16(claim, PCI_COMMAND,
              (claim->saved_command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) |
              PCI_COMMAND_INTX_DISABLE);
  failure = "invalid or unassigned BAR";
  if (!pci_size_bars(claim)) {
    goto fail;
  }

  failure = "invalid or missing register capability";
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    unsigned id = pci_read8(device->address, offset);
    if (id == PCI_CAP_VENDOR && !read_virtio_capability(pci, offset, device_bytes, device_alignment)) {
      goto fail;
    }
  }
  if (!pci_msix_discover(claim, &pci->msix) || !regions_disjoint(pci, device_bytes != 0)) {
    goto fail;
  }

  failure = "cannot map register resources";
  if (!map_region(pci, "common", &pci->common, VIRTIO_COMMON_BYTES, boot) ||
      !map_region(pci, "notify", &pci->notify, pci->notify.length, boot) ||
      !map_region(pci, "ISR", &pci->isr, VIRTIO_ISR_BYTES, boot) ||
      (device_bytes && !map_region(pci, "device", &pci->device, pci->device.length, boot)) ||
      pci_msix_map(&pci->msix, boot) != MM_OK) {
    goto fail;
  }

  pci_write16(claim, PCI_COMMAND,
              (claim->saved_command & ~(PCI_COMMAND_IO | PCI_COMMAND_MASTER)) |
              PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE);
  /* Reading status has no acknowledgement side effect, unlike the ISR byte. */
  unsigned status = virtio_pci_common(pci)->device_status;
  if (status) {
    failure = "device did not remain reset";
    goto fail;
  }
  transport_log(pci, "%s PCI: register resources owned; %u MSI-X entries, DMA and interrupts disabled\n",
       pci->name, pci->msix.entries);

  return true;

fail:
  transport_log(pci, "%s PCI: %s; releasing resources with DMA and interrupts disabled\n",
       pci->name, failure);
  pci_release_device(claim);
  const char *name = pci->name;
  *pci = (struct virtio_pci_transport){.name = name};
  return false;
}
