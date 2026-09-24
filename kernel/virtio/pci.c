#include <arch/clock.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/pci/registers.h>
#include <kernel/virtio/pci.h>

#define VIRTIO_VENDOR_ID 0x1af4
#define VIRTIO_PCI_DEVICE_BASE 0x1040
#define VIRTIO_FS_DEVICE_ID 26
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
#define VIRTIO_RESET_TIMEOUT_NS UINT64_C(1000000000)
#define VIRTIO_COMMON_BYTES 56
#define VIRTIO_COMMON_STATUS 20
#define VIRTIO_NOTIFY_BYTES 2
#define VIRTIO_ISR_BYTES 1
#define VIRTIO_FS_CONFIG_BYTES 40

struct virtio_pci_region {
  unsigned bar;
  uint32_t offset, length;
  struct pci_mapping mapping;
};

static struct {
  struct pci_claim claim;
  struct virtio_pci_region common, notify, isr, device;
  struct virtio_pci_region msix_table, msix_pba;
  unsigned msix_capability, msix_entries;
  uint32_t notify_multiplier;
  bool ready;
} filesystem;

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
static bool reset_before_probe(void)
{
  struct pci_claim *claim = &filesystem.claim;
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
    klog("virtio-fs PCI: reset confirmed before BAR probing\n");
  }
  return stopped;
}

static bool region_fits(const struct virtio_pci_region *region)
{
  return region->bar < PCI_BAR_COUNT && region->length &&
    region->offset <= filesystem.claim.bars[region->bar].bytes &&
    region->length <= filesystem.claim.bars[region->bar].bytes - region->offset;
}

static bool read_virtio_capability(unsigned offset)
{
  struct pci_claim *claim = &filesystem.claim;
  struct pci_address address = claim->device->address;
  if (!pci_capability_fits(claim, offset, VIRTIO_CAP_BAR)) {
    return false;
  }
  unsigned type = pci_read8(address, offset + VIRTIO_CAP_TYPE);
  struct virtio_pci_region *region;
  unsigned minimum, alignment;
  switch (type) {
  case VIRTIO_COMMON_CONFIG:
    region = &filesystem.common;
    minimum = VIRTIO_COMMON_BYTES;
    alignment = 4;
    break;
  case VIRTIO_NOTIFY_CONFIG:
    region = &filesystem.notify;
    minimum = VIRTIO_NOTIFY_BYTES;
    alignment = 2;
    break;
  case VIRTIO_ISR_CONFIG:
    region = &filesystem.isr;
    minimum = VIRTIO_ISR_BYTES;
    alignment = 1;
    break;
  case VIRTIO_DEVICE_CONFIG:
    region = &filesystem.device;
    minimum = VIRTIO_FS_CONFIG_BYTES;
    alignment = 4;
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
  *region = (struct virtio_pci_region){
    .bar = bar,
    .offset = pci_read32(address, offset + VIRTIO_CAP_OFFSET),
    .length = pci_read32(address, offset + VIRTIO_CAP_REGION_LENGTH),
  };
  if (!region_fits(region) || region->length < minimum || region->offset % alignment) {
    return false;
  }
  if (type == VIRTIO_NOTIFY_CONFIG) {
    filesystem.notify_multiplier = pci_read32(address, offset + VIRTIO_NOTIFY_MULTIPLIER);
    if (filesystem.notify_multiplier % alignment) {
      return false;
    }
  }
  return true;
}

static bool read_msix_capability(unsigned offset)
{
  struct pci_claim *claim = &filesystem.claim;
  if (filesystem.msix_capability || !pci_capability_fits(claim, offset, PCI_MSIX_BYTES)) {
    return false;
  }
  struct pci_address address = claim->device->address;
  unsigned control = pci_read16(address, offset + PCI_MSIX_CONTROL);
  unsigned entries = (control & PCI_MSIX_SIZE_MASK) + 1;
  uint32_t table = pci_read32(address, offset + PCI_MSIX_TABLE);
  uint32_t pba = pci_read32(address, offset + PCI_MSIX_PBA);
  filesystem.msix_table = (struct virtio_pci_region){
    .bar = table & PCI_MSIX_BAR_MASK, .offset = table & PCI_MSIX_OFFSET_MASK,
    .length = entries * PCI_MSIX_ENTRY_BYTES,
  };
  filesystem.msix_pba = (struct virtio_pci_region){
    .bar = pba & PCI_MSIX_BAR_MASK, .offset = pba & PCI_MSIX_OFFSET_MASK,
    .length = ((entries + PCI_MSIX_PBA_BITS - 1) / PCI_MSIX_PBA_BITS) * PCI_MSIX_PBA_WORD_BYTES,
  };
  filesystem.msix_capability = offset;
  filesystem.msix_entries = entries;
  return region_fits(&filesystem.msix_table) && region_fits(&filesystem.msix_pba);
}

static bool regions_disjoint(void)
{
  const struct virtio_pci_region *regions[] = {
    &filesystem.common, &filesystem.notify, &filesystem.isr, &filesystem.device,
    &filesystem.msix_table, &filesystem.msix_pba,
  };
  for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
    const struct virtio_pci_region *a = regions[i];
    if (!region_fits(a)) {
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      const struct virtio_pci_region *b = regions[j];
      if (a->bar == b->bar && a->offset < (uint64_t)b->offset + b->length &&
          b->offset < (uint64_t)a->offset + a->length) {
        return false;
      }
    }
  }
  return true;
}

static bool map_region(const char *name, struct virtio_pci_region *region,
                       size_t needed, const struct boot_info *boot)
{
  if (!region_fits(region)) {
    return false;
  }
  enum mm_result result = pci_map_bar(&filesystem.claim, region->bar, region->offset,
                                      needed, boot, &region->mapping);
  if (result != MM_OK) {
    klog("virtio-fs PCI: cannot map %s (error %u)\n", name, (unsigned)result);
    return false;
  }
  klog("virtio-fs PCI: %s BAR%u offset=0x%x bytes=0x%zx address=%p\n",
       name, region->bar, region->offset, needed, (void *)region->mapping.address);
  return true;
}

void virtio_fs_pci_prepare(const struct boot_info *boot)
{
  struct pci_device *device = pci_find_device(VIRTIO_VENDOR_ID,
                                              VIRTIO_PCI_DEVICE_BASE + VIRTIO_FS_DEVICE_ID);
  if (!device) {
    return;
  }
  struct pci_claim *claim = &filesystem.claim;
  if (!pci_claim_device(device, claim)) {
    klog("virtio-fs PCI: function busy or unsupported; resources not claimed\n");
    return;
  }
  const char *failure = "cannot confirm initial reset";
  if (!reset_before_probe()) {
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
    if ((id == PCI_CAP_VENDOR && !read_virtio_capability(offset)) ||
        (id == PCI_CAP_MSIX && !read_msix_capability(offset))) {
      goto fail;
    }
  }
  if (!filesystem.msix_capability || !regions_disjoint()) {
    goto fail;
  }

  failure = "cannot map register resources";
  if (!map_region("common", &filesystem.common, VIRTIO_COMMON_BYTES, boot) ||
      !map_region("notify", &filesystem.notify, filesystem.notify.length, boot) ||
      !map_region("ISR", &filesystem.isr, VIRTIO_ISR_BYTES, boot) ||
      !map_region("device", &filesystem.device, VIRTIO_FS_CONFIG_BYTES, boot) ||
      !map_region("MSI-X table", &filesystem.msix_table, filesystem.msix_table.length, boot) ||
      !map_region("MSI-X pending bits", &filesystem.msix_pba, filesystem.msix_pba.length, boot)) {
    goto fail;
  }

  pci_write16(claim, PCI_COMMAND,
              (claim->saved_command & ~(PCI_COMMAND_IO | PCI_COMMAND_MASTER)) |
              PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE);
  /* Reading status has no acknowledgement side effect, unlike the ISR byte. */
  unsigned status = *(const volatile uint8_t *)(filesystem.common.mapping.address +
                                               VIRTIO_COMMON_STATUS);
  if (status) {
    failure = "device did not remain reset";
    goto fail;
  }
  filesystem.ready = true;
  klog("virtio-fs PCI: register resources owned; %u MSI-X entries, DMA and interrupts disabled\n",
       filesystem.msix_entries);
  return;

fail:
  klog("virtio-fs PCI: %s; releasing resources with DMA and interrupts disabled\n", failure);
  pci_release_device(claim);
  filesystem = (typeof(filesystem)){0};
}
