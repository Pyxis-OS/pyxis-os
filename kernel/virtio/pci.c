#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/fs/hostfs.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>
#include <kernel/virtio/pci.h>
#include <kernel/virtio/transport.h>
#include <kernel/virtio/fs.h>
#include <kernel/virtio/queue.h>
#include <stddef.h>

#define VIRTIO_FS_DEVICE_ID 26
#define VIRTIO_FS_CONFIG_BYTES 40
#define VIRTIO_FS_TAG_BYTES 36
#define VIRTIO_FS_HIPRIO_QUEUE 0
/* No notification queue: VIRTIO_FS_F_NOTIFICATION is not negotiated. */
#define VIRTIO_FS_FIRST_REQUEST_QUEUE 1
#define VIRTIO_FS_MSIX_ENTRY 0
#define VIRTIO_REQUEST_TIMEOUT_MS 5000u

struct virtio_fs_config {
  uint8_t tag[VIRTIO_FS_TAG_BYTES];
  uint32_t num_request_queues;
};

_Static_assert(sizeof(struct virtio_fs_config) == VIRTIO_FS_CONFIG_BYTES,
               "VirtIO filesystem configuration layout");

static struct {
  struct virtio_pci_transport pci;
  uint64_t offered_features, accepted_features;
  char tag[VIRTIO_FS_TAG_BYTES + 1];
  uint32_t request_queues;
  struct virtio_queue_info hiprio_info, request_info;
  struct virtqueue hiprio, request;
  struct virtio_fs_session session;
  bool negotiated, prepared, active;
  uint8_t config_generation;
  bool interrupt_ready, interrupt_pending;
  struct task_wait *interrupt_wait;
} filesystem;

static volatile struct virtio_pci_common *common_config(void)
{
  return virtio_pci_common(&filesystem.pci);
}

void virtio_fs_pci_wake(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  /* IRQ, BSP request publication and worker all use IF=0. Remember work even
   * after device failure: deferred local cleanup must still run. */
  filesystem.interrupt_pending = true;
  struct task_wait *wait = filesystem.interrupt_wait;
  filesystem.interrupt_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void virtio_fs_pci_interrupt(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (filesystem.interrupt_ready) {
    virtio_fs_pci_wake();
  }
}

static void wait_interrupt(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(cpu_current() == cpu_bsp() && (flags & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!filesystem.interrupt_wait);

  while (!filesystem.interrupt_pending && !task_deadline_expired(deadline)) {
    struct task_wait *wait = task_wait_prepare();
    filesystem.interrupt_wait = wait;
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    filesystem.interrupt_wait = NULL;
  }
  filesystem.interrupt_pending = false;
  cpu_restore_interrupts(flags);
}

static bool disable_msix(void)
{
  filesystem.interrupt_ready = false;
  return virtio_pci_disable_msix(&filesystem.pci);
}

static bool prepare_msix(void)
{
  if (!virtio_pci_prepare_msix(&filesystem.pci, APIC_VIRTIO_FS_VECTOR)) {
    return false;
  }
  volatile struct virtio_pci_common *common = common_config();
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
  return true;
}

static bool read_filesystem_config(void)
{
  volatile struct virtio_pci_common *common = common_config();
  const volatile struct virtio_fs_config *config =
    (const volatile struct virtio_fs_config *)filesystem.pci.device.mapping.address;
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
  if (!virtio_pci_inspect_queue(&filesystem.pci, VIRTIO_FS_HIPRIO_QUEUE, &filesystem.hiprio_info) ||
      !virtio_pci_inspect_queue(&filesystem.pci, VIRTIO_FS_FIRST_REQUEST_QUEUE, &filesystem.request_info)) {
    return "required queue unavailable, enabled or outside notification region";
  }
  if (common->device_status != status) {
    return "device status changed during queue inspection";
  }
  filesystem.negotiated = true;
  return NULL;
}

static bool configure_queue(struct virtqueue *queue)
{
  volatile struct virtio_pci_common *common = common_config();
  common->queue_select = queue->index;
  if (common->queue_enable) {
    return false;
  }
  common->queue_size = queue->size;
  phys_addr_t descriptors = virtqueue_descriptor_address(queue);
  phys_addr_t available = virtqueue_available_address(queue);
  phys_addr_t used = virtqueue_used_address(queue);
  common->queue_desc_low = (uint32_t)descriptors;
  common->queue_desc_high = descriptors >> 32;
  common->queue_driver_low = (uint32_t)available;
  common->queue_driver_high = available >> 32;
  common->queue_device_low = (uint32_t)used;
  common->queue_device_high = used >> 32;
  if (common->queue_size != queue->size ||
      common->queue_desc_low != (uint32_t)descriptors || common->queue_desc_high != descriptors >> 32 ||
      common->queue_driver_low != (uint32_t)available || common->queue_driver_high != available >> 32 ||
      common->queue_device_low != (uint32_t)used || common->queue_device_high != used >> 32 ||
      common->queue_msix_vector != VIRTIO_FS_MSIX_ENTRY) {
    return false;
  }

  dma_write_barrier();
  common->queue_enable = 1;
  if (common->queue_enable != 1) {
    return false;
  }
  klog("virtio-fs PCI: queue %u size=%u descriptors=0x%lx available=0x%lx used=0x%lx\n",
       (unsigned)queue->index, (unsigned)queue->size, descriptors, available, used);
  return true;
}

static const char *prepare_queues(void)
{
  enum mm_result result = virtqueue_allocate(&filesystem.hiprio, VIRTIO_FS_HIPRIO_QUEUE,
      filesystem.hiprio_info.max_size, filesystem.hiprio_info.notify_address);
  if (result == MM_OK) {
    result = virtqueue_allocate(&filesystem.request, VIRTIO_FS_FIRST_REQUEST_QUEUE,
        filesystem.request_info.max_size, filesystem.request_info.notify_address);
  }
  if (result != MM_OK) {
    klog("virtio-fs PCI: queue allocation failed (error %u)\n", (unsigned)result);
    return "cannot allocate queue storage";
  }
  if (!configure_queue(&filesystem.hiprio) || !configure_queue(&filesystem.request)) {
    return "queue configuration rejected";
  }
  filesystem.config_generation = common_config()->config_generation;
  filesystem.prepared = true;
  return NULL;
}

static bool activate_transport(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_claim *claim = &filesystem.pci.claim;
  volatile struct virtio_pci_common *common = common_config();
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  bool ready = common->device_status == expected;
  if (ready) {
    uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
    pci_write16(claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
    ready = (pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  }
  if (ready) {
    common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    ready = common->device_status == (expected | VIRTIO_STATUS_DRIVER_OK);
  }
  if (ready) {
    volatile struct pci_msix_entry *table =
      (volatile struct pci_msix_entry *)filesystem.pci.msix_table.mapping.address;
    table[VIRTIO_FS_MSIX_ENTRY].control &= ~PCI_MSIX_VECTOR_MASK;
    ready = !(table[VIRTIO_FS_MSIX_ENTRY].control & PCI_MSIX_VECTOR_MASK);
  }
  if (ready) {
    unsigned offset = filesystem.pci.msix_capability + PCI_MSIX_CONTROL;
    uint16_t control = pci_read16(claim->device->address, offset);
    pci_write16(claim, offset, control & ~PCI_MSIX_FUNCTION_MASK);
    control = pci_read16(claim->device->address, offset);
    ready = (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_ENABLE;
  }
  filesystem.active = ready;
  cpu_restore_interrupts(flags);
  return ready;
}

static const char *transport_failure(void)
{
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
    VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
  if (common_config()->device_status != expected) {
    return "device status changed or reset requested";
  }
  /* No live reconfiguration yet: a changed tag/queue layout needs a new session. */
  if (common_config()->config_generation != filesystem.config_generation) {
    return "device configuration changed";
  }
  return NULL;
}

static void stop_transport(const char *failure);

static enum virtio_fs_result request_failure(enum virtio_fs_result result, const char *reason)
{
  stop_transport(reason);
  return result;
}

static enum virtio_fs_result exchange_queue(struct virtqueue *queue, struct virtqueue *other,
    const void *request, size_t request_bytes, void *reply, size_t reply_capacity, size_t *reply_bytes)
{
  KASSERT(cpu_current() == cpu_bsp());
  *reply_bytes = 0;
  if (!filesystem.active) {
    return VIRTIO_FS_UNAVAILABLE;
  }
  if (queue->in_flight || other->in_flight || !request_bytes ||
      request_bytes > VIRTQUEUE_REQUEST_BYTES || reply_capacity > VIRTQUEUE_REPLY_BYTES) {
    return VIRTIO_FS_INVALID;
  }
  uint64_t deadline = task_deadline_after_ms(VIRTIO_REQUEST_TIMEOUT_MS);
  memcpy(queue->request, request, request_bytes);
  if (!virtqueue_submit(queue, request_bytes, reply_capacity)) {
    return VIRTIO_FS_INVALID;
  }

  for (;;) {
    const char *failure = transport_failure();
    if (failure) {
      return request_failure(VIRTIO_FS_UNAVAILABLE, failure);
    }
    size_t ignored;
    if (virtqueue_complete(other, &ignored) != VIRTQUEUE_PENDING) {
      return request_failure(VIRTIO_FS_PROTOCOL, "unexpected completion on idle queue");
    }
    enum virtqueue_result result = virtqueue_complete(queue, reply_bytes);
    if (result == VIRTQUEUE_BROKEN) {
      return request_failure(VIRTIO_FS_PROTOCOL, "invalid queue completion");
    }
    if (result == VIRTQUEUE_COMPLETE) {
      if (*reply_bytes) {
        memcpy(reply, queue->reply, *reply_bytes);
      }
      return VIRTIO_FS_OK;
    }
    if (task_deadline_expired(deadline)) {
      return request_failure(VIRTIO_FS_TIMED_OUT, "request timed out");
    }
    wait_interrupt(deadline);
  }
}

enum virtio_fs_result virtio_fs_pci_request(const void *request, size_t request_bytes,
    void *reply, size_t reply_capacity, size_t *reply_bytes)
{
  return exchange_queue(&filesystem.request, &filesystem.hiprio,
      request, request_bytes, reply, reply_capacity, reply_bytes);
}

enum virtio_fs_result virtio_fs_pci_forget(const void *request, size_t request_bytes)
{
  size_t ignored;
  /* No FUSE reply, but the used-ring completion still returns DMA ownership. */
  return exchange_queue(&filesystem.hiprio, &filesystem.request,
      request, request_bytes, NULL, 0, &ignored);
}

static void stop_transport(const char *failure)
{
  /* Shared kernel mappings stay live after AP startup. Even a successful reset
   * does not authorize unmapping them without an SMP invalidation contract. */
  uint64_t flags = cpu_save_interrupts();
  filesystem.active = false;
  filesystem.prepared = false;
  filesystem.session.ready = false;
  KASSERT(!filesystem.interrupt_wait);
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)filesystem.pci.msix_table.mapping.address;
  table[VIRTIO_FS_MSIX_ENTRY].control |= PCI_MSIX_VECTOR_MASK;
  (void)table[VIRTIO_FS_MSIX_ENTRY].control;
  bool interrupts_disabled = disable_msix();
  struct pci_claim *claim = &filesystem.pci.claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  bool dma_disabled = !(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  common_config()->device_status |= VIRTIO_STATUS_FAILED;
  common_config()->device_status = 0;
  cpu_restore_interrupts(flags);

  uint64_t deadline = task_deadline_after_ms(VIRTIO_RESET_TIMEOUT_NS / UINT64_C(1000000));
  while (common_config()->device_status && !task_deadline_expired(deadline)) {
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  bool reset = common_config()->device_status == 0;
  if (reset) {
    filesystem.hiprio.in_flight = false;
    filesystem.request.in_flight = false;
  }
  klog("virtio-fs: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", failure, (unsigned)reset,
       (unsigned)interrupts_disabled, (unsigned)dma_disabled);
}

void virtio_fs_pci_stop(const char *reason)
{
  if (filesystem.active) {
    stop_transport(reason);
  }
}

/* Set before resource preparation; a failed device is not an absent device. */
static bool filesystem_present;

bool virtio_fs_pci_present(void)
{
  return filesystem_present;
}

static void filesystem_worker(void *argument)
{
  (void)argument;
  if (!activate_transport()) {
    stop_transport("cannot activate transport");
    hostfs_start_failed(VIRTIO_FS_UNAVAILABLE);
    return;
  }
  enum virtio_fs_result result = virtio_fs_session_init(&filesystem.session);
  if (result != VIRTIO_FS_OK) {
    klog("virtio-fs: session initialization failed (result %u)\n", (unsigned)result);
    virtio_fs_pci_stop("FUSE INIT failed");
    hostfs_start_failed(result);
    return;
  }
  klog("virtio-fs: tag=\"%s\" FUSE %u.%u session ready, optional features=0\n",
       filesystem.tag, filesystem.session.major, filesystem.session.minor);

  hostfs_start(&filesystem.session);
  for (;;) {
    if (filesystem.active) {
      const char *failure = transport_failure();
      size_t ignored;
      if (!failure && (virtqueue_complete(&filesystem.hiprio, &ignored) != VIRTQUEUE_PENDING ||
                       virtqueue_complete(&filesystem.request, &ignored) != VIRTQUEUE_PENDING)) {
        failure = "unexpected completion while idle";
      }
      if (failure) {
        stop_transport(failure);
      }
    }
    if (!hostfs_service()) {
      wait_interrupt(UINT64_MAX);
    }
  }
}

void virtio_fs_pci_start(void)
{
  if (!filesystem.prepared) {
    return;
  }
  hostfs_prepare();
  enum mm_result result = kernel_task_create(filesystem_worker, NULL);
  if (result != MM_OK) {
    filesystem.prepared = false;
    hostfs_start_failed(VIRTIO_FS_NO_MEMORY);
    klog("virtio-fs: cannot create worker (error %u); DMA remains disabled, "
         "resources retained until reboot\n", (unsigned)result);
  }
}

void virtio_fs_pci_prepare(const struct boot_info *boot)
{
  struct pci_device *device = pci_find_device(VIRTIO_VENDOR_ID,
                                              VIRTIO_PCI_DEVICE_BASE + VIRTIO_FS_DEVICE_ID);
  if (!device) {
    return;
  }
  filesystem_present = true;
  struct pci_claim *claim = &filesystem.pci.claim;
  filesystem.pci.name = "virtio-fs";
  if (!virtio_pci_prepare(&filesystem.pci, device, boot, VIRTIO_FS_CONFIG_BYTES, 4)) {
    return;
  }

  const char *failure = negotiate_transport();
  if (!failure && !prepare_msix()) {
    failure = "MSI-X routing rejected";
  }
  if (!failure) {
    failure = prepare_queues();
  }
  if (failure) {
    filesystem.prepared = false;
    bool interrupts_disabled = disable_msix();
    filesystem.negotiated = false;
    klog("virtio-fs PCI: %s; marking FAILED and resetting\n", failure);
    common_config()->device_status |= VIRTIO_STATUS_FAILED;
    bool reset = virtio_pci_reset(&filesystem.pci);
    if (!reset || !interrupts_disabled) {
      /* Published queue storage cannot be released without confirmed reset. */
      klog("virtio-fs PCI: cleanup unconfirmed (reset=%u MSI-X disabled=%u); "
           "claim and mappings retained until reboot, DMA disabled\n",
           (unsigned)reset, (unsigned)interrupts_disabled);
      return;
    }
    goto fail;
  }
  klog("virtio-fs PCI: queues prepared; DMA and delivery disabled, awaiting BSP worker\n");
  return;

fail:
  klog("virtio-fs PCI: %s; releasing resources with DMA and interrupts disabled\n", failure);
  virtqueue_release(&filesystem.request);
  virtqueue_release(&filesystem.hiprio);
  pci_release_device(claim);
  filesystem = (typeof(filesystem)){0};
}
