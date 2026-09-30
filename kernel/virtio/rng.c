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
#include <kernel/task.h>
#include <kernel/virtio/rng.h>
#include <kernel/virtio/transport.h>
#include <kernel/virtio/queue.h>
#include <stdatomic.h>

#define VIRTIO_RNG_DEVICE_ID 4
#define VIRTIO_RNG_QUEUE 0
#define VIRTIO_RNG_MSIX_ENTRY 0
#define VIRTIO_RNG_QUEUE_SIZE 16u
#define VIRTIO_RNG_BUFFER_BYTES 8192u
/* The DMA request survives caller expiry and reuse of its call slot. */
#define VIRTIO_RNG_DMA_REQUEST_ID 0
#define RNG_CALL_LIMIT 8
#define RNG_DEVICE_TIMEOUT_MS 5000

enum rng_call_state { RNG_FREE, RNG_QUEUED, RNG_ACTIVE, RNG_DONE };
struct rng_call {
  enum rng_call_state state;
  size_t length, filled;
  uint64_t deadline;
  uint8_t bytes[RANDOM_MAX_BYTES];
  enum call_status status;
  struct task_wait *wait;
  bool cancelled;
};

static struct {
  struct virtio_pci_transport pci;
  struct virtqueue queue;
  struct dma_buffer buffer;
  struct virtio_queue_info queue_info;
  bool prepared, active, interrupt_ready;
  uint64_t dma_deadline;
  struct rng_call *current; /* Worker only; detach before completing a call. */
} entropy;

/* Shared request/notification state. All users hold the lock with IF=0.
 * DONE still owns its slot until the original caller consumes it. */
static struct rng_call calls[RNG_CALL_LIMIT];
static atomic_bool locked;
static bool accepting, notified;
static struct task_wait *worker_wait;

static void lock_rng(void)
{
  while (atomic_exchange_explicit(&locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_rng(void)
{
  atomic_store_explicit(&locked, false, memory_order_release);
}

static volatile struct virtio_pci_common *common_config(void)
{
  return virtio_pci_common(&entropy.pci);
}

/* Lock held, IF=0. Remember wakeups arriving before worker wait publication. */
static void notify_worker(void)
{
  notified = true;
  struct task_wait *wait = worker_wait;
  worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void virtio_rng_interrupt(void)
{
  if (entropy.interrupt_ready) {
    lock_rng();
    notify_worker();
    unlock_rng();
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

static bool activate_transport(void)
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
    volatile struct pci_msix_entry *table =
      (volatile struct pci_msix_entry *)entropy.pci.msix_table.mapping.address;
    table[VIRTIO_RNG_MSIX_ENTRY].control &= ~PCI_MSIX_VECTOR_MASK;
    ready = !(table[VIRTIO_RNG_MSIX_ENTRY].control & PCI_MSIX_VECTOR_MASK);
  }
  if (ready) {
    unsigned offset = entropy.pci.msix_capability + PCI_MSIX_CONTROL;
    uint16_t control = pci_read16(claim->device->address, offset);
    pci_write16(claim, offset, control & ~PCI_MSIX_FUNCTION_MASK);
    control = pci_read16(claim->device->address, offset);
    ready = (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_ENABLE;
  }
  entropy.active = ready;
  cpu_restore_interrupts(flags);
  return ready;
}

/* Lock held. The caller may consume and reuse the slot as soon as we unlock. */
static void complete_call(struct rng_call *call, enum call_status status)
{
  call->status = status;
  call->state = RNG_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
}

enum call_status virtio_rng_read(void *bytes, size_t length, uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (length > RANDOM_MAX_BYTES) {
    return CALL_LIMIT;
  }
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (deadline - now > RANDOM_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  if (!length) {
    return CALL_OK;
  }

  lock_rng();
  if (!accepting) {
    unlock_rng();
    return CALL_UNAVAILABLE;
  }
  struct rng_call *call = NULL;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_FREE) {
      call = &calls[i];
      break;
    }
  }
  if (!call) {
    unlock_rng();
    return CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct rng_call){
    .state = RNG_QUEUED, .length = length, .deadline = deadline, .wait = wait,
  };
  notify_worker();
  unlock_rng();
  task_wait_sleep_interruptible(wait);

  lock_rng();
  while (call->state != RNG_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    notify_worker();
    unlock_rng();
    task_wait_sleep(wait);
    lock_rng();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK) {
    memcpy(bytes, call->bytes, length);
  }
  *call = (struct rng_call){0};
  unlock_rng();
  return status;
}

static void stop_transport(const char *reason)
{
  virtqueue_stop(&entropy.queue);
  uint64_t flags = cpu_save_interrupts();
  entropy.active = false;
  entropy.current = NULL;
  lock_rng();
  accepting = false;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_QUEUED || calls[i].state == RNG_ACTIVE) {
      complete_call(&calls[i], CALL_UNAVAILABLE);
    }
  }
  unlock_rng();
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
    memset((void *)entropy.buffer.address, 0, VIRTIO_RNG_BUFFER_BYTES);
  }
  /* Keep shared mappings and DMA storage until reboot, even after reset. */
  klog("virtio-rng: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", reason, (unsigned)reset,
       (unsigned)interrupts_disabled, (unsigned)dma_disabled);
}

static bool expire_calls(void)
{
  bool worked = false;
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    struct rng_call *call = &calls[i];
    if ((call->state == RNG_QUEUED || call->state == RNG_ACTIVE) &&
        (call->cancelled || task_deadline_expired(call->deadline))) {
      if (entropy.current == call) {
        /* The DMA buffer is separate. A late completion will be discarded,
         * never attributed to a caller that reuses this slot. */
        entropy.current = NULL;
      }
      complete_call(call, call->cancelled ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT);
      worked = true;
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
  return worked;
}

static void finish_chunk(size_t length)
{
  struct rng_call *call = entropy.current;
  if (call) {
    KASSERT(length <= call->length - call->filled);
    memcpy(call->bytes + call->filled, (const void *)entropy.buffer.address, length);
    call->filled += length;
    if (call->filled == call->length) {
      uint64_t flags = cpu_save_interrupts();
      lock_rng();
      entropy.current = NULL;
      complete_call(call, task_deadline_expired(call->deadline) ? CALL_TIMED_OUT : CALL_OK);
      unlock_rng();
      cpu_restore_interrupts(flags);
    }
  }
  /* Only the used ring returned ownership. Never read/reuse a pending buffer. */
  memset((void *)entropy.buffer.address, 0, VIRTIO_RNG_BUFFER_BYTES);
}

static void select_call(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_QUEUED) {
      calls[i].state = RNG_ACTIVE;
      entropy.current = &calls[i];
      break;
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
}

static void wait_for_work(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  uint64_t deadline = entropy.queue.outstanding ? entropy.dma_deadline : UINT64_MAX;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if ((calls[i].state == RNG_QUEUED || calls[i].state == RNG_ACTIVE) &&
        calls[i].deadline < deadline) {
      deadline = calls[i].deadline;
    }
  }
  if (notified) {
    notified = false;
    unlock_rng();
  } else {
    struct task_wait *wait = task_wait_prepare();
    KASSERT(!worker_wait);
    worker_wait = wait;
    unlock_rng();
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    lock_rng();
    worker_wait = NULL;
    notified = false;
    unlock_rng();
  }
  cpu_restore_interrupts(flags);
}

static void entropy_worker(void *argument)
{
  (void)argument;
  if (!activate_transport()) {
    stop_transport("cannot activate transport");
    return;
  }
  klog("virtio-rng: host random source ready, demand-driven reads\n");
  for (;;) {
    bool worked = expire_calls();
    unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
        VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    if (common_config()->device_status != expected) {
      stop_transport("device status changed or reset requested");
      return;
    }
    struct virtqueue_completion completion;
    size_t count;
    enum virtqueue_result result = virtqueue_complete(&entropy.queue, &completion, 1, &count);
    if ((result != VIRTQUEUE_PENDING && result != VIRTQUEUE_COMPLETE) ||
        (result == VIRTQUEUE_COMPLETE && (count != 1 ||
         completion.request_id != VIRTIO_RNG_DMA_REQUEST_ID || !completion.written))) {
      stop_transport("invalid entropy completion");
      return;
    }
    if (result == VIRTQUEUE_COMPLETE) {
      finish_chunk(completion.written);
      worked = true;
    }
    if (entropy.queue.outstanding && task_deadline_expired(entropy.dma_deadline)) {
      stop_transport("device request timed out");
      return;
    }
    if (!entropy.queue.outstanding) {
      if (!entropy.current) {
        select_call();
      }
      struct rng_call *call = entropy.current;
      if (call) {
        if (!task_deadline_expired(call->deadline)) {
          entropy.dma_deadline = task_deadline_after_ms(RNG_DEVICE_TIMEOUT_MS);
          struct virtqueue_segment segment = {
            .physical = entropy.buffer.physical,
            .bytes = call->length - call->filled,
            .access = VIRTQUEUE_DEVICE_WRITE,
          };
          KASSERT(virtqueue_submit(&entropy.queue, &segment, 1,
              VIRTIO_RNG_DMA_REQUEST_ID) == VIRTQUEUE_ACCEPTED);
          virtqueue_notify(&entropy.queue);
        }
        worked = true;
      }
    }
    if (worked) {
      /* Bound completion/short-fill work before yielding to other BSP tasks. */
      kernel_task_sleep_until(arch_monotonic_ns());
    } else {
      wait_for_work();
    }
  }
}

void virtio_rng_start(void)
{
  if (!entropy.prepared) {
    return;
  }
  enum mm_result result = kernel_task_create(entropy_worker, NULL);
  if (result != MM_OK) {
    klog("virtio-rng: cannot create worker (error %u); DMA disabled, resources retained\n",
         (unsigned)result);
    return;
  }
  accepting = true; /* Before schedulers start; queued callers await activation. */
}

void virtio_rng_prepare(const struct boot_info *boot)
{
  struct pci_device *device = pci_find_device(VIRTIO_VENDOR_ID,
      VIRTIO_PCI_DEVICE_BASE + VIRTIO_RNG_DEVICE_ID);
  if (!device) {
    klog("virtio-rng: no device; random reads unavailable\n");
    return;
  }
  entropy.pci.name = "virtio-rng";
  if (!virtio_pci_prepare(&entropy.pci, device, boot, 0, 1)) {
    return;
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
    return;
  }
  entropy.prepared = true;
  klog("virtio-rng PCI: queue prepared; DMA and delivery disabled until worker start\n");
}
