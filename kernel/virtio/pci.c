#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>
#include <kernel/virtio/pci.h>
#include <stddef.h>

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
#define VIRTIO_CONFIG_TIMEOUT_NS UINT64_C(1000000000)
#define VIRTIO_COMMON_BYTES 56
#define VIRTIO_NOTIFY_BYTES 2
#define VIRTIO_ISR_BYTES 1
#define VIRTIO_FS_CONFIG_BYTES 40
#define VIRTIO_FS_TAG_BYTES 36
#define VIRTIO_FS_HIPRIO_QUEUE 0
/* No notification queue: VIRTIO_FS_F_NOTIFICATION is not negotiated. */
#define VIRTIO_FS_FIRST_REQUEST_QUEUE 1
#define VIRTIO_FEATURE_WORD_BITS 32
#define VIRTIO_F_VERSION_1 (UINT64_C(1) << 32)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u
#define VIRTIO_FS_MSIX_ENTRY 0

struct pci_msix_entry {
  uint32_t address_low, address_high, data, control;
};

_Static_assert(sizeof(struct pci_msix_entry) == PCI_MSIX_ENTRY_BYTES,
               "PCI MSI-X table entry layout");

/* Naturally aligned, little-endian registers in the common configuration.
 * The prefix ends before queue addresses, which this stage does not program. */
struct virtio_pci_common {
  uint32_t device_feature_select, device_feature;
  uint32_t driver_feature_select, driver_feature;
  uint16_t config_msix_vector, num_queues;
  uint8_t device_status, config_generation;
  uint16_t queue_select, queue_size, queue_msix_vector, queue_enable;
  uint16_t queue_notify_off;
};

struct virtio_fs_config {
  uint8_t tag[VIRTIO_FS_TAG_BYTES];
  uint32_t num_request_queues;
};

_Static_assert(offsetof(struct virtio_pci_common, device_status) == 20 &&
               offsetof(struct virtio_pci_common, queue_notify_off) == 30,
               "VirtIO PCI common register layout");
_Static_assert(sizeof(struct virtio_fs_config) == VIRTIO_FS_CONFIG_BYTES,
               "VirtIO filesystem configuration layout");

#define VIRTIO_COMMON_STATUS offsetof(struct virtio_pci_common, device_status)

struct virtio_pci_region {
  unsigned bar;
  uint32_t offset, length;
  struct pci_mapping mapping;
};

struct virtio_queue_info {
  uint16_t max_size;
  uintptr_t notify_address;
};

static struct {
  struct pci_claim claim;
  struct virtio_pci_region common, notify, isr, device;
  struct virtio_pci_region msix_table, msix_pba;
  unsigned msix_capability, msix_entries;
  uint32_t notify_multiplier;
  uint64_t offered_features, accepted_features;
  char tag[VIRTIO_FS_TAG_BYTES + 1];
  uint32_t request_queues;
  struct virtio_queue_info hiprio, request;
  bool negotiated;
  bool interrupt_ready, interrupt_pending;
  struct task_wait *interrupt_wait;
} filesystem;

static volatile struct virtio_pci_common *common_config(void)
{
  return (volatile struct virtio_pci_common *)filesystem.common.mapping.address;
}

void virtio_fs_pci_interrupt(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (!filesystem.interrupt_ready) {
    return;
  }

  /* Worker and IRQ both run on the BSP: IF=0 protects this handoff. Activity
   * survives an interrupt before publication of the worker's wait record. */
  filesystem.interrupt_pending = true;
  struct task_wait *wait = filesystem.interrupt_wait;
  filesystem.interrupt_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void virtio_fs_pci_wait_interrupt(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(cpu_current() == cpu_bsp() && (flags & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(filesystem.interrupt_ready && !filesystem.interrupt_wait);

  while (!filesystem.interrupt_pending) {
    struct task_wait *wait = task_wait_prepare();
    filesystem.interrupt_wait = wait;
    task_wait_sleep(wait);
    filesystem.interrupt_wait = NULL;
  }
  filesystem.interrupt_pending = false;
  cpu_restore_interrupts(flags);
}

static bool disable_msix(void)
{
  struct pci_claim *claim = &filesystem.claim;
  unsigned offset = filesystem.msix_capability + PCI_MSIX_CONTROL;
  uint16_t control = pci_read16(claim->device->address, offset);
  pci_write16(claim, offset, (control | PCI_MSIX_FUNCTION_MASK) & ~PCI_MSIX_ENABLE);
  filesystem.interrupt_ready = false;
  control = pci_read16(claim->device->address, offset);
  return (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_FUNCTION_MASK;
}

static bool prepare_msix(void)
{
  struct pci_claim *claim = &filesystem.claim;
  unsigned offset = filesystem.msix_capability + PCI_MSIX_CONTROL;
  uint16_t control = pci_read16(claim->device->address, offset);
  unsigned masked_enable = PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK;
  /* VirtIO only accepts vector mappings while MSI-X is enabled. Keep the
   * function mask set throughout setup, including all failure paths. */
  pci_write16(claim, offset, control | masked_enable);
  if ((pci_read16(claim->device->address, offset) & masked_enable) != masked_enable) {
    return false;
  }

  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)filesystem.msix_table.mapping.address;
  for (unsigned i = 0; i < filesystem.msix_entries; ++i) {
    table[i].control |= PCI_MSIX_VECTOR_MASK;
    if (!(table[i].control & PCI_MSIX_VECTOR_MASK)) {
      return false;
    }
  }

  struct apic_msi_message message = apic_bsp_msi_message(APIC_VIRTIO_FS_VECTOR);
  volatile struct pci_msix_entry *entry = &table[VIRTIO_FS_MSIX_ENTRY];
  entry->address_low = message.address_low;
  entry->address_high = message.address_high;
  entry->data = message.data;
  /* Read back device writes before relying on the route. All register mappings
   * are uncached; table accesses use individual aligned 32-bit transactions. */
  if (entry->address_low != message.address_low || entry->address_high != message.address_high ||
      entry->data != message.data || !(entry->control & PCI_MSIX_VECTOR_MASK)) {
    return false;
  }

  /* These registers contain table indices, not CPU interrupt vectors. A
   * rejected mapping reads back as NO_VECTOR (0xffff), even for a valid index. */
  volatile struct virtio_pci_common *common = common_config();
  common->config_msix_vector = VIRTIO_FS_MSIX_ENTRY;
  if (common->config_msix_vector != VIRTIO_FS_MSIX_ENTRY) {
    return false;
  }
  common->queue_select = VIRTIO_FS_HIPRIO_QUEUE;
  common->queue_msix_vector = VIRTIO_FS_MSIX_ENTRY;
  if (common->queue_msix_vector != VIRTIO_FS_MSIX_ENTRY) {
    return false;
  }
  common->queue_select = VIRTIO_FS_FIRST_REQUEST_QUEUE;
  common->queue_msix_vector = VIRTIO_FS_MSIX_ENTRY;
  if (common->queue_msix_vector != VIRTIO_FS_MSIX_ENTRY) {
    return false;
  }

  filesystem.interrupt_ready = true;
  klog("virtio-fs PCI: MSI-X entry %u -> BSP APIC %u vector %u; config and queues share masked route\n",
       VIRTIO_FS_MSIX_ENTRY, cpu_bsp()->lapic_id, APIC_VIRTIO_FS_VECTOR);
  return true;
}

static bool reset_mapped_device(void)
{
  volatile struct virtio_pci_common *common = common_config();
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

static bool read_filesystem_config(void)
{
  volatile struct virtio_pci_common *common = common_config();
  const volatile struct virtio_fs_config *config =
    (const volatile struct virtio_fs_config *)filesystem.device.mapping.address;
  uint64_t start = arch_monotonic_ns();
  do {
    /* A tag and queue count must come from the same configuration generation. */
    uint8_t generation = common->config_generation;
    for (size_t i = 0; i < VIRTIO_FS_TAG_BYTES; ++i) {
      filesystem.tag[i] = config->tag[i];
    }
    filesystem.request_queues = config->num_request_queues;
    if (common->config_generation == generation) {
      /* A full-width device tag has no terminator on the wire. */
      filesystem.tag[VIRTIO_FS_TAG_BYTES] = '\0';
      return true;
    }
  } while (arch_monotonic_ns() - start < VIRTIO_CONFIG_TIMEOUT_NS);
  return false;
}

static bool inspect_queue(unsigned index, struct virtio_queue_info *queue)
{
  volatile struct virtio_pci_common *common = common_config();
  common->queue_select = index;
  uint16_t size = common->queue_size;
  if (!size || (size & (size - 1)) || common->queue_enable) {
    return false;
  }

  /* queue_notify_off is in multiplier units, not bytes. Widen before multiplying. */
  uint64_t offset = (uint64_t)common->queue_notify_off * filesystem.notify_multiplier;
  if (offset > filesystem.notify.length ||
      VIRTIO_NOTIFY_BYTES > filesystem.notify.length - offset) {
    return false;
  }
  *queue = (struct virtio_queue_info){
    .max_size = size,
    .notify_address = filesystem.notify.mapping.address + offset,
  };
  klog("virtio-fs PCI: queue %u maximum=%u notify=%p, disabled\n",
       index, (unsigned)size, (void *)queue->notify_address);
  return true;
}

static const char *negotiate_transport(void)
{
  volatile struct virtio_pci_common *common = common_config();
  common->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
  common->device_status |= VIRTIO_STATUS_DRIVER;

  common->device_feature_select = 0;
  uint64_t low = common->device_feature;
  common->device_feature_select = 1;
  filesystem.offered_features = low | ((uint64_t)common->device_feature << VIRTIO_FEATURE_WORD_BITS);
  klog("virtio-fs PCI: offered features[63:0]=0x%lx\n", filesystem.offered_features);
  if (!(filesystem.offered_features & VIRTIO_F_VERSION_1)) {
    return "VIRTIO_F_VERSION_1 is required";
  }

  /* Accept only the modern baseline: split queues, direct physical addresses,
   * and 16-bit queue notifications. Reset leaves all other feature words zero. */
  filesystem.accepted_features = VIRTIO_F_VERSION_1;
  common->driver_feature_select = 0;
  common->driver_feature = (uint32_t)filesystem.accepted_features;
  common->driver_feature_select = 1;
  common->driver_feature = filesystem.accepted_features >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != status) {
    return "feature negotiation rejected or device needs reset";
  }
  klog("virtio-fs PCI: accepted VIRTIO_F_VERSION_1, FEATURES_OK confirmed\n");

  if (!read_filesystem_config()) {
    return "filesystem configuration did not stabilize";
  }
  unsigned queues = common->num_queues;
  if (queues < 2 || !filesystem.request_queues || filesystem.request_queues > queues - 1) {
    return "invalid filesystem request queue count";
  }
  klog("virtio-fs PCI: tag=\"%s\", request queues=%u, transport queues=%u\n",
       filesystem.tag, filesystem.request_queues, queues);
  if (!inspect_queue(VIRTIO_FS_HIPRIO_QUEUE, &filesystem.hiprio) ||
      !inspect_queue(VIRTIO_FS_FIRST_REQUEST_QUEUE, &filesystem.request)) {
    return "required queue unavailable, enabled or outside notification region";
  }
  if (common->device_status != status) {
    return "device status changed during queue inspection";
  }
  filesystem.negotiated = true;
  return NULL;
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
  unsigned status = common_config()->device_status;
  if (status) {
    failure = "device did not remain reset";
    goto fail;
  }
  klog("virtio-fs PCI: register resources owned; %u MSI-X entries, DMA and interrupts disabled\n",
       filesystem.msix_entries);

  failure = negotiate_transport();
  if (!failure && !prepare_msix()) {
    failure = "MSI-X routing rejected";
  }
  if (failure) {
    bool interrupts_disabled = disable_msix();
    filesystem.negotiated = false;
    klog("virtio-fs PCI: %s; marking FAILED and resetting\n", failure);
    common_config()->device_status |= VIRTIO_STATUS_FAILED;
    bool reset = reset_mapped_device();
    if (!reset || !interrupts_disabled) {
      /* No queues or DMA buffers exist, but keep the failed function claimed:
       * an unconfirmed reset must not look like an available device. */
      klog("virtio-fs PCI: cleanup unconfirmed (reset=%u MSI-X disabled=%u); "
           "claim and mappings retained until reboot, DMA disabled\n",
           (unsigned)reset, (unsigned)interrupts_disabled);
      return;
    }
    goto fail;
  }
  klog("virtio-fs PCI: transport negotiated; queues inactive, DRIVER_OK clear\n");
  return;

fail:
  klog("virtio-fs PCI: %s; releasing resources with DMA and interrupts disabled\n", failure);
  pci_release_device(claim);
  filesystem = (typeof(filesystem)){0};
}
