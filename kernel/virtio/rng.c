#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/random.h>
#include <kernel/task.h>
#include <kernel/virtio/rng.h>
#include <kernel/virtio/transport.h>
#include <kernel/virtio/queue.h>

#define VIRTIO_RNG_DEVICE_ID 4
#define VIRTIO_RNG_QUEUE 0
#define VIRTIO_RNG_MSIX_ENTRY 0
#define VIRTIO_RNG_QUEUE_SIZE 16u
#define VIRTIO_RNG_BUFFER_BYTES 8192u
#define RNG_DEVICE_TIMEOUT_MS 5000
/* The DMA request survives caller expiry and reuse of its call slot. */
#define VIRTIO_RNG_DMA_REQUEST_ID 0

static struct {
  struct virtio_pci_transport pci;
  struct virtqueue queue;
  struct dma_buffer buffer;
  struct virtio_queue_info queue_info;
  bool interrupt_ready;
  uint64_t dma_deadline;
} entropy;

static volatile struct virtio_pci_common *common_config(void)
{
  return virtio_pci_common(&entropy.pci);
}

void virtio_rng_interrupt(void)
{
  if (entropy.interrupt_ready) {
    random_notify();
  }
}

static bool disable_msix(void)
{
  entropy.interrupt_ready = false;
  return virtio_pci_disable_msix(&entropy.pci);
}

static bool prepare_msix(void)
{
  if (!virtio_pci_prepare_msix(&entropy.pci, APIC_VIRTIO_RNG_VECTOR)) {
    return false;
  }
  volatile struct virtio_pci_common *common = common_config();
  common->queue_select = VIRTIO_RNG_QUEUE;
  common->queue_msix_vector = VIRTIO_RNG_MSIX_ENTRY;
  if (common->queue_msix_vector != VIRTIO_RNG_MSIX_ENTRY) {
    return false;
  }
  entropy.interrupt_ready = true;
  return true;
}

static const char *negotiate_transport(void)
{
  volatile struct virtio_pci_common *common = common_config();
  common->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
  common->device_status |= VIRTIO_STATUS_DRIVER;
  common->device_feature_select = 1;
  uint64_t offered = (uint64_t)common->device_feature << VIRTIO_FEATURE_WORD_BITS;
  if (!(offered & VIRTIO_F_VERSION_1)) {
    return "VIRTIO_F_VERSION_1 is required";
  }
  /* Only the modern baseline: split queue, direct DMA, no device features. */
  common->driver_feature_select = 0;
  common->driver_feature = 0;
  common->driver_feature_select = 1;
  common->driver_feature = VIRTIO_F_VERSION_1 >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != expected) {
    return "feature negotiation rejected or device needs reset";
  }
  if (!common->num_queues ||
      !virtio_pci_inspect_queue(&entropy.pci, VIRTIO_RNG_QUEUE, &entropy.queue_info)) {
    return "entropy queue unavailable or outside notification region";
  }
  return common->device_status == expected ? NULL : "device status changed during preparation";
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
      common->queue_msix_vector != VIRTIO_RNG_MSIX_ENTRY) {
    return false;
  }

  dma_write_barrier();
  common->queue_enable = 1;
  if (common->queue_enable != 1) {
    return false;
  }
  klog("virtio-rng PCI: queue %u size=%u descriptors=0x%lx available=0x%lx used=0x%lx\n",
       (unsigned)queue->index, (unsigned)queue->size, descriptors, available, used);
  return true;
}

bool virtio_rng_activate(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_claim *claim = &entropy.pci.claim;
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
    ready = pci_msix_enable(&entropy.pci.msix);
  }
  cpu_restore_interrupts(flags);
  return ready;
}

void virtio_rng_stop(const char *reason)
{
  virtqueue_stop(&entropy.queue);
  uint64_t flags = cpu_save_interrupts();
  bool interrupts_disabled = disable_msix();
  struct pci_claim *claim = &entropy.pci.claim;
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
    virtqueue_confirm_reset(&entropy.queue);
    memzero_explicit((void *)entropy.buffer.address, VIRTIO_RNG_BUFFER_BYTES);
  }
  /* Keep shared mappings and DMA storage until reboot, even after reset. */
  klog("virtio-rng: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", reason, (unsigned)reset,
       (unsigned)interrupts_disabled, (unsigned)dma_disabled);
}

enum virtio_rng_result virtio_rng_poll(void *bytes, size_t capacity, size_t *length)
{
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
      VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
  if (common_config()->device_status != expected) {
    return VIRTIO_RNG_FAILED;
  }
  struct virtqueue_completion completion;
  size_t count;
  enum virtqueue_result result = virtqueue_complete(&entropy.queue, &completion, 1, &count);
  if ((result != VIRTQUEUE_PENDING && result != VIRTQUEUE_COMPLETE) ||
      (result == VIRTQUEUE_COMPLETE && (count != 1 ||
       completion.request_id != VIRTIO_RNG_DMA_REQUEST_ID || !completion.written))) {
    return VIRTIO_RNG_FAILED;
  }
  if (result == VIRTQUEUE_COMPLETE) {
    if (bytes) {
      KASSERT(completion.written <= capacity);
      memcpy(bytes, (const void *)entropy.buffer.address, completion.written);
    }
    *length = completion.written;
    /* Only a checked used entry returned ownership, including discarded data. */
    memzero_explicit((void *)entropy.buffer.address, VIRTIO_RNG_BUFFER_BYTES);
    return VIRTIO_RNG_COMPLETE;
  }
  if (entropy.queue.outstanding && task_deadline_expired(entropy.dma_deadline)) {
    return VIRTIO_RNG_FAILED;
  }
  return entropy.queue.outstanding ? VIRTIO_RNG_PENDING : VIRTIO_RNG_IDLE;
}

void virtio_rng_submit(size_t length)
{
  KASSERT(!entropy.queue.outstanding && length && length <= RANDOM_MAX_BYTES);
  entropy.dma_deadline = task_deadline_after_ms(RNG_DEVICE_TIMEOUT_MS);
  struct virtqueue_segment segment = {
    .physical = entropy.buffer.physical,
    .bytes = length,
    .access = VIRTQUEUE_DEVICE_WRITE,
  };
  KASSERT(virtqueue_submit(&entropy.queue, &segment, 1,
      VIRTIO_RNG_DMA_REQUEST_ID) == VIRTQUEUE_ACCEPTED);
  virtqueue_notify(&entropy.queue);
}

uint64_t virtio_rng_deadline(void)
{
  return entropy.queue.outstanding ? entropy.dma_deadline : UINT64_MAX;
}

enum virtio_rng_preparation virtio_rng_prepare(const struct boot_info *boot)
{
  struct pci_device *device = pci_find_device(VIRTIO_VENDOR_ID,
      VIRTIO_PCI_DEVICE_BASE + VIRTIO_RNG_DEVICE_ID);
  if (!device) {
    return VIRTIO_RNG_ABSENT;
  }
  entropy.pci.name = "virtio-rng";
  if (!virtio_pci_prepare(&entropy.pci, device, boot, 0, 1)) {
    return VIRTIO_RNG_UNAVAILABLE;
  }
  const char *failure = negotiate_transport();
  if (!failure && !prepare_msix()) {
    failure = "MSI-X routing rejected";
  }
  if (!failure && entropy.queue_info.max_size < 2) {
    failure = "entropy queue requires at least two descriptors";
  }
  if (!failure) {
    unsigned maximum = entropy.queue_info.max_size;
    unsigned size = maximum < VIRTIO_RNG_QUEUE_SIZE ? maximum : VIRTIO_RNG_QUEUE_SIZE;
    enum mm_result result = virtqueue_allocate(&entropy.queue, VIRTIO_RNG_QUEUE,
        size, maximum, entropy.queue_info.notify_address);
    if (result == MM_OK) {
      result = dma_buffer_allocate(&entropy.buffer, VIRTIO_RNG_BUFFER_BYTES);
    }
    if (result != MM_OK || !configure_queue(&entropy.queue)) {
      failure = "cannot allocate or configure entropy queue";
    }
  }
  if (failure) {
    bool interrupts_disabled = disable_msix();
    common_config()->device_status |= VIRTIO_STATUS_FAILED;
    bool reset = virtio_pci_reset(&entropy.pci);
    klog("virtio-rng PCI: %s (reset=%u MSI-X disabled=%u)\n", failure,
         (unsigned)reset, (unsigned)interrupts_disabled);
    if (reset && interrupts_disabled) {
      dma_buffer_release(&entropy.buffer);
      virtqueue_release(&entropy.queue);
      pci_release_device(&entropy.pci.claim);
      entropy = (typeof(entropy)){0};
    }
    return VIRTIO_RNG_UNAVAILABLE;
  }
  klog("virtio-rng PCI: queue prepared; DMA and delivery disabled until worker start\n");
  return VIRTIO_RNG_PREPARED;
}
