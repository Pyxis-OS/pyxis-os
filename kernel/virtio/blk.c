#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/block.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/virtio/blk.h>
#include <kernel/virtio/queue.h>
#include <kernel/virtio/transport.h>

#define VIRTIO_BLK_DEVICE_ID 2
#define VIRTIO_BLK_TRANSITIONAL_PCI_ID 0x1001
#define VIRTIO_BLK_QUEUE 0
#define VIRTIO_BLK_MSIX_ENTRY 0
#define VIRTIO_BLK_QUEUE_SIZE 32u
#define BLK_SLOT_LIMIT 8u
#define BLK_TRANSFER_LIMIT 65536u
#define BLK_REQUEST_DESCRIPTORS 3u
#define BLK_SECTOR_BYTES 512u
#define BLK_LARGE_BLOCK_BYTES 4096u
#define BLK_DEVICE_TIMEOUT_MS 5000u
#define VIRTIO_BLK_F_SIZE_MAX (UINT64_C(1) << 1)
#define VIRTIO_BLK_F_SEG_MAX (UINT64_C(1) << 2)
#define VIRTIO_BLK_F_RO (UINT64_C(1) << 5)
#define VIRTIO_BLK_F_BLK_SIZE (UINT64_C(1) << 6)
#define VIRTIO_BLK_F_FLUSH (UINT64_C(1) << 9)
#define VIRTIO_BLK_T_IN 0u
#define VIRTIO_BLK_T_OUT 1u
#define VIRTIO_BLK_T_FLUSH 4u
#define VIRTIO_BLK_S_OK 0u
#define VIRTIO_BLK_S_IOERR 1u
#define VIRTIO_BLK_S_UNSUPP 2u
#define VIRTIO_BLK_STATUS_UNWRITTEN UINT8_MAX

struct blk_config {
  uint32_t capacity_low, capacity_high, size_max, seg_max;
  uint16_t cylinders;
  uint8_t heads, sectors;
  uint32_t blk_size;
};

struct blk_header {
  uint32_t type, reserved;
  uint64_t sector;
};

_Static_assert(offsetof(struct blk_config, blk_size) == 20, "block configuration layout");
_Static_assert(sizeof(struct blk_header) == 16, "block request layout");

enum blk_state { BLK_FREE, BLK_QUEUED, BLK_ACTIVE, BLK_DONE };
struct blk_slot {
  struct dma_buffer control, data;
  enum blk_state state;
  enum block_operation operation;
  uint64_t generation, first_block, dma_deadline;
  size_t bytes;
  struct block_completion completion;
  struct task_wait *wait;
  bool abandoned, device_owned;
};

static struct {
  struct virtio_pci_transport pci;
  struct virtqueue queue;
  struct virtio_queue_info queue_info;
  struct block_info info;
  struct blk_slot slots[BLK_SLOT_LIMIT];
  uint64_t features, generation;
  uint64_t published, completed, reordered;
  unsigned peak_outstanding;
  uint8_t config_generation;
  bool prepared, active, accepting, interrupt_ready, notified;
  struct task_wait *worker_wait;
} disk;

static enum block_preparation preparation = BLOCK_INVENTORY_INCOMPLETE;

static volatile struct virtio_pci_common *common_config(void)
{
  return virtio_pci_common(&disk.pci);
}

static void assert_client_context(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

enum block_preparation block_preparation_result(void)
{
  assert_client_context();
  return preparation;
}

/* BSP/IF=0; clients and IRQ entry cannot race the worker's parking handoff. */
static void notify_worker(void)
{
  disk.notified = true;
  struct task_wait *wait = disk.worker_wait;
  disk.worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void virtio_blk_interrupt(void)
{
  if (disk.interrupt_ready) {
    notify_worker();
  }
}

static bool disable_msix(void)
{
  disk.interrupt_ready = false;
  return virtio_pci_disable_msix(&disk.pci);
}

static bool prepare_msix(void)
{
  if (!virtio_pci_prepare_msix(&disk.pci, APIC_VIRTIO_BLK_VECTOR)) {
    return false;
  }
  common_config()->queue_select = VIRTIO_BLK_QUEUE;
  common_config()->queue_msix_vector = VIRTIO_BLK_MSIX_ENTRY;
  if (common_config()->queue_msix_vector != VIRTIO_BLK_MSIX_ENTRY) {
    return false;
  }
  disk.interrupt_ready = true;
  return true;
}

static const char *read_geometry(void)
{
  volatile struct blk_config *config = (volatile struct blk_config *)disk.pci.device.mapping.address;
  size_t needed = sizeof(uint64_t);
  if (disk.features & VIRTIO_BLK_F_SIZE_MAX) {
    needed = offsetof(struct blk_config, size_max) + sizeof(config->size_max);
  }
  if (disk.features & VIRTIO_BLK_F_SEG_MAX) {
    needed = offsetof(struct blk_config, seg_max) + sizeof(config->seg_max);
  }
  if (disk.features & VIRTIO_BLK_F_BLK_SIZE) {
    needed = sizeof(*config);
  }
  if (disk.pci.device.length < needed) {
    return "device configuration too short";
  }
  uint64_t deadline = arch_monotonic_ns() + VIRTIO_CONFIG_TIMEOUT_NS;
  uint64_t sectors;
  uint32_t block_size, size_max, seg_max;
  for (;;) {
    uint8_t generation = common_config()->config_generation;
    sectors = config->capacity_low;
    sectors |= (uint64_t)config->capacity_high << 32;
    size_max = disk.features & VIRTIO_BLK_F_SIZE_MAX ? config->size_max : BLK_TRANSFER_LIMIT;
    seg_max = disk.features & VIRTIO_BLK_F_SEG_MAX ? config->seg_max : 1;
    block_size = disk.features & VIRTIO_BLK_F_BLK_SIZE ? config->blk_size : BLK_SECTOR_BYTES;
    if (common_config()->config_generation == generation) {
      disk.config_generation = generation;
      break;
    }
    if (arch_monotonic_ns() >= deadline) {
      return "unstable device configuration";
    }
    __asm__ volatile("pause");
  }
  if (block_size != BLK_SECTOR_BYTES && block_size != BLK_LARGE_BLOCK_BYTES) {
    return "unsupported logical block size";
  }
  if (!sectors || sectors > UINT64_MAX / BLK_SECTOR_BYTES ||
      sectors % (block_size / BLK_SECTOR_BYTES) || !seg_max || size_max < block_size) {
    return "invalid capacity or transfer limits";
  }
  uint32_t transfer = size_max < BLK_TRANSFER_LIMIT ? size_max : BLK_TRANSFER_LIMIT;
  transfer -= transfer % block_size;
  disk.info = (struct block_info){
    .block_count = sectors / (block_size / BLK_SECTOR_BYTES),
    .block_size = block_size,
    .max_transfer = transfer,
    .writable = !(disk.features & VIRTIO_BLK_F_RO),
    .flush_supported = (disk.features & VIRTIO_BLK_F_FLUSH) != 0,
  };
  return NULL;
}

static const char *negotiate_transport(void)
{
  volatile struct virtio_pci_common *common = common_config();
  common->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
  common->device_status |= VIRTIO_STATUS_DRIVER;
  common->device_feature_select = 0;
  uint64_t offered = common->device_feature;
  common->device_feature_select = 1;
  offered |= (uint64_t)common->device_feature << VIRTIO_FEATURE_WORD_BITS;
  if (!(offered & VIRTIO_F_VERSION_1)) {
    return "modern VirtIO is required";
  }
  if (!(offered & VIRTIO_BLK_F_RO) && !(offered & VIRTIO_BLK_F_FLUSH)) {
    return "writable devices require flush support";
  }
  disk.features = offered & (VIRTIO_F_VERSION_1 | VIRTIO_BLK_F_SIZE_MAX |
      VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH);
  common->driver_feature_select = 0;
  common->driver_feature = disk.features;
  common->driver_feature_select = 1;
  common->driver_feature = disk.features >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != expected) {
    return "feature negotiation rejected or device needs reset";
  }
  if (!common->num_queues ||
      !virtio_pci_inspect_queue(&disk.pci, VIRTIO_BLK_QUEUE, &disk.queue_info)) {
    return "block queue unavailable";
  }
  return read_geometry();
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
      common->queue_msix_vector != VIRTIO_BLK_MSIX_ENTRY) {
    return false;
  }

  dma_write_barrier();
  common->queue_enable = 1;
  if (common->queue_enable != 1) {
    return false;
  }
  klog("virtio-blk PCI: queue %u size=%u descriptors=0x%lx available=0x%lx used=0x%lx\n",
       (unsigned)queue->index, (unsigned)queue->size, descriptors, available, used);
  return true;
}

static bool activate_transport(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_claim *claim = &disk.pci.claim;
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
      (volatile struct pci_msix_entry *)disk.pci.msix_table.mapping.address;
    table[VIRTIO_BLK_MSIX_ENTRY].control &= ~PCI_MSIX_VECTOR_MASK;
    ready = !(table[VIRTIO_BLK_MSIX_ENTRY].control & PCI_MSIX_VECTOR_MASK);
  }
  if (ready) {
    unsigned offset = disk.pci.msix_capability + PCI_MSIX_CONTROL;
    uint16_t control = pci_read16(claim->device->address, offset);
    pci_write16(claim, offset, control & ~PCI_MSIX_FUNCTION_MASK);
    control = pci_read16(claim->device->address, offset);
    ready = (control & (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_ENABLE;
  }
  disk.active = ready;
  cpu_restore_interrupts(flags);
  return ready;
}

static struct blk_slot *find_ticket(const struct block_ticket *ticket)
{
  if (!ticket || ticket->slot >= disk.info.request_slots || !ticket->generation) {
    return NULL;
  }
  struct blk_slot *slot = &disk.slots[ticket->slot];
  return slot->state != BLK_FREE && !slot->abandoned &&
      slot->generation == ticket->generation ? slot : NULL;
}

enum block_result block_get_info(struct block_info *info)
{
  assert_client_context();
  if (!info) {
    return BLOCK_INVALID;
  }
  if (!disk.accepting) {
    return BLOCK_UNAVAILABLE;
  }
  *info = disk.info;
  return BLOCK_OK;
}

enum block_result block_submit(enum block_operation operation, uint64_t first_block,
    uint32_t block_count, const void *write_bytes, struct block_ticket *ticket)
{
  assert_client_context();
  if (!ticket || (operation != BLOCK_READ && operation != BLOCK_WRITE && operation != BLOCK_FLUSH) ||
      (operation == BLOCK_WRITE ? !write_bytes : write_bytes != NULL)) {
    return BLOCK_INVALID;
  }
  if (!disk.accepting || disk.generation == UINT64_MAX) {
    return BLOCK_UNAVAILABLE;
  }
  if (operation != BLOCK_READ && !disk.info.writable) {
    return BLOCK_READ_ONLY;
  }
  if (operation != BLOCK_READ && disk.info.write_failed) {
    return BLOCK_WRITE_FAILED;
  }
  size_t bytes = 0;
  if (operation == BLOCK_FLUSH) {
    if (first_block || block_count) {
      return BLOCK_INVALID;
    }
  } else {
    if (!block_count || block_count > disk.info.max_transfer / disk.info.block_size ||
        first_block >= disk.info.block_count || block_count > disk.info.block_count - first_block) {
      return BLOCK_INVALID;
    }
    bytes = (size_t)block_count * disk.info.block_size;
  }
  unsigned index = 0;
  while (index < disk.info.request_slots && disk.slots[index].state != BLK_FREE) {
    ++index;
  }
  if (index == disk.info.request_slots) {
    return BLOCK_FULL;
  }
  struct blk_slot *slot = &disk.slots[index];
  KASSERT(!slot->device_owned && !slot->wait);
  struct dma_buffer control = slot->control, data = slot->data;
  *slot = (struct blk_slot){
    .control = control, .data = data, .state = BLK_QUEUED,
    .operation = operation, .generation = ++disk.generation,
    .first_block = first_block, .bytes = bytes,
  };
  if (operation == BLOCK_WRITE) {
    memcpy((void *)data.address, write_bytes, bytes);
  }
  *ticket = (struct block_ticket){.generation = slot->generation, .slot = index};
  notify_worker();
  return BLOCK_OK;
}

enum block_result block_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion)
{
  assert_client_context();
  struct blk_slot *slot = find_ticket(ticket);
  if (!slot || !completion) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  if (slot->state != BLK_DONE) {
    return BLOCK_PENDING;
  }
  if (slot->operation == BLOCK_READ && slot->completion.result == BLOCK_OK) {
    if (!read_bytes || read_capacity < slot->bytes) {
      return BLOCK_INVALID;
    }
    KASSERT(!slot->device_owned);
    memcpy(read_bytes, (const void *)slot->data.address, slot->bytes);
  }
  *completion = slot->completion;
  slot->state = BLK_FREE;
  return BLOCK_OK;
}

enum block_result block_abandon(const struct block_ticket *ticket)
{
  assert_client_context();
  struct blk_slot *slot = find_ticket(ticket);
  if (!slot) {
    return BLOCK_INVALID;
  }
  if (slot->wait) {
    return BLOCK_BUSY;
  }
  slot->abandoned = true;
  if (slot->state != BLK_ACTIVE) {
    slot->state = BLK_FREE;
  }
  notify_worker();
  return BLOCK_OK;
}

enum block_result block_wait(const struct block_ticket *ticket, uint64_t deadline_ns)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  for (;;) {
    struct blk_slot *slot = find_ticket(ticket);
    enum block_result result;
    if (!slot) {
      result = BLOCK_INVALID;
    } else if (slot->wait) {
      result = BLOCK_BUSY;
    } else if (slot->state == BLK_DONE) {
      result = BLOCK_OK;
    } else if (task_deadline_expired(deadline_ns)) {
      result = BLOCK_TIMED_OUT;
    } else {
      struct task_wait *wait = task_wait_prepare();
      slot->wait = wait;
      task_wait_sleep_until(wait, deadline_ns);
      /* No slot pointer is retained across a wake that could let its owner
       * consume it. An invalidated ticket cannot detach a new generation. */
      slot = find_ticket(ticket);
      if (slot && slot->wait == wait) {
        slot->wait = NULL;
      }
      continue;
    }
    cpu_restore_interrupts(flags);
    return result;
  }
}

/* BSP/IF=0. Wake only after removing the shared reference to task metadata. */
static void finish_slot(struct blk_slot *slot, enum block_result result)
{
  slot->completion.result = result;
  slot->completion.bytes = result == BLOCK_OK ? slot->bytes : 0;
  slot->state = slot->abandoned ? BLK_FREE : BLK_DONE;
  struct task_wait *wait = slot->wait;
  slot->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void stop_transport(const char *reason)
{
  virtqueue_stop(&disk.queue);
  uint64_t flags = cpu_save_interrupts();
  disk.accepting = false;
  disk.active = false;
  for (unsigned i = 0; i < disk.info.request_slots; ++i) {
    struct blk_slot *slot = &disk.slots[i];
    if (slot->state == BLK_QUEUED || slot->state == BLK_ACTIVE) {
      finish_slot(slot, BLOCK_UNAVAILABLE);
    }
  }
  bool interrupts_disabled = disable_msix();
  struct pci_claim *claim = &disk.pci.claim;
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
    virtqueue_confirm_reset(&disk.queue);
    flags = cpu_save_interrupts();
    for (unsigned i = 0; i < disk.info.request_slots; ++i) {
      disk.slots[i].device_owned = false;
    }
    cpu_restore_interrupts(flags);
  }
  klog("virtio-blk: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", reason, (unsigned)reset,
       (unsigned)interrupts_disabled, (unsigned)dma_disabled);
}

static const char *complete_requests(bool *worked)
{
  struct virtqueue_completion completed[BLK_SLOT_LIMIT];
  size_t count;
  enum virtqueue_result result = virtqueue_complete(&disk.queue, completed, BLK_SLOT_LIMIT, &count);
  if (result == VIRTQUEUE_PENDING) {
    return NULL;
  }
  if (result != VIRTQUEUE_COMPLETE) {
    return "invalid queue completion";
  }
  *worked = true;
  for (size_t i = 0; i < count; ++i) {
    uint64_t flags = cpu_save_interrupts();
    struct blk_slot *slot = NULL;
    uint64_t oldest = UINT64_MAX;
    for (unsigned n = 0; n < disk.info.request_slots; ++n) {
      struct blk_slot *candidate = &disk.slots[n];
      if (candidate->state == BLK_ACTIVE) {
        if (candidate->generation < oldest) {
          oldest = candidate->generation;
        }
        if (candidate->generation == completed[i].request_id) {
          slot = candidate;
        }
      }
    }
    if (!slot || !slot->device_owned || !slot->completion.submitted) {
      cpu_restore_interrupts(flags);
      return "unknown request completion";
    }
    uint8_t status = *(volatile uint8_t *)(slot->control.address + sizeof(struct blk_header));
    uint32_t expected = slot->operation == BLOCK_READ ? slot->bytes + 1 : 1;
    if (!completed[i].written || status > VIRTIO_BLK_S_UNSUPP ||
        (status == VIRTIO_BLK_S_OK && completed[i].written != expected)) {
      cpu_restore_interrupts(flags);
      return "invalid block status or completion length";
    }
    slot->device_owned = false;
    ++disk.completed;
    if (slot->generation != oldest) {
      ++disk.reordered;
    }
    enum block_result outcome = status == VIRTIO_BLK_S_OK ? BLOCK_OK :
        status == VIRTIO_BLK_S_IOERR ? BLOCK_IO_ERROR : BLOCK_UNSUPPORTED;
    if (outcome != BLOCK_OK && slot->operation != BLOCK_READ) {
      disk.info.write_failed = true;
    }
    finish_slot(slot, outcome);
    cpu_restore_interrupts(flags);
  }
  return NULL;
}

static const char *submit_requests(bool *worked)
{
  bool notify = false;
  for (unsigned issued = 0; issued < disk.info.request_slots; ++issued) {
    uint64_t flags = cpu_save_interrupts();
    struct blk_slot *slot = NULL;
    bool flushing = false;
    for (unsigned i = 0; i < disk.info.request_slots; ++i) {
      struct blk_slot *candidate = &disk.slots[i];
      if (candidate->state == BLK_ACTIVE && candidate->operation == BLOCK_FLUSH) {
        flushing = true;
      }
      if (candidate->state == BLK_QUEUED && (!slot || candidate->generation < slot->generation)) {
        slot = candidate;
      }
    }
    if (!slot || flushing || (slot->operation == BLOCK_FLUSH && disk.queue.outstanding)) {
      cpu_restore_interrupts(flags);
      break;
    }
    if (disk.info.write_failed && slot->operation != BLOCK_READ) {
      finish_slot(slot, BLOCK_WRITE_FAILED);
      *worked = true;
      cpu_restore_interrupts(flags);
      continue;
    }
    /* ACTIVE prevents a preempting client from recycling the slot while the
     * worker builds and publishes descriptors with interrupts enabled. */
    slot->state = BLK_ACTIVE;
    cpu_restore_interrupts(flags);
    struct blk_header *header = (struct blk_header *)slot->control.address;
    *header = (struct blk_header){
      .type = slot->operation == BLOCK_READ ? VIRTIO_BLK_T_IN :
          slot->operation == BLOCK_WRITE ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_FLUSH,
      .sector = slot->first_block * (disk.info.block_size / BLK_SECTOR_BYTES),
    };
    *(uint8_t *)(slot->control.address + sizeof(*header)) = VIRTIO_BLK_STATUS_UNWRITTEN;
    struct virtqueue_segment segments[BLK_REQUEST_DESCRIPTORS] = {
      {.physical = slot->control.physical, .bytes = sizeof(*header), .access = VIRTQUEUE_DEVICE_READ},
    };
    size_t count = 1;
    if (slot->operation != BLOCK_FLUSH) {
      segments[count++] = (struct virtqueue_segment){
        .physical = slot->data.physical, .bytes = slot->bytes,
        .access = slot->operation == BLOCK_READ ? VIRTQUEUE_DEVICE_WRITE : VIRTQUEUE_DEVICE_READ,
      };
    }
    segments[count++] = (struct virtqueue_segment){
      .physical = slot->control.physical + sizeof(*header), .bytes = 1,
      .access = VIRTQUEUE_DEVICE_WRITE,
    };
    if (virtqueue_submit(&disk.queue, segments, count, slot->generation) != VIRTQUEUE_ACCEPTED) {
      return "cannot publish admitted block request";
    }
    flags = cpu_save_interrupts();
    slot->completion.submitted = true;
    slot->device_owned = true;
    slot->dma_deadline = task_deadline_after_ms(BLK_DEVICE_TIMEOUT_MS);
    ++disk.published;
    if (disk.queue.outstanding > disk.peak_outstanding) {
      disk.peak_outstanding = disk.queue.outstanding;
    }
    cpu_restore_interrupts(flags);
    notify = true;
    *worked = true;
  }
  if (notify) {
    virtqueue_notify(&disk.queue);
  }
  return NULL;
}

static void wait_for_work(void)
{
  uint64_t flags = cpu_save_interrupts();
  uint64_t deadline = UINT64_MAX;
  for (unsigned i = 0; i < disk.info.request_slots; ++i) {
    struct blk_slot *slot = &disk.slots[i];
    if (slot->state == BLK_ACTIVE && slot->dma_deadline < deadline) {
      deadline = slot->dma_deadline;
    }
  }
  if (disk.notified) {
    disk.notified = false;
  } else {
    KASSERT(!disk.worker_wait);
    disk.worker_wait = task_wait_prepare();
    if (deadline == UINT64_MAX) {
      task_wait_sleep(disk.worker_wait);
    } else {
      task_wait_sleep_until(disk.worker_wait, deadline);
    }
    disk.worker_wait = NULL;
    disk.notified = false;
  }
  cpu_restore_interrupts(flags);
}

static void block_worker(void *argument)
{
  (void)argument;
  if (!activate_transport()) {
    stop_transport("cannot activate transport");
    return;
  }
  klog("virtio-blk: ready, %lu blocks of %u bytes, transfer=%u slots=%u %s\n",
       disk.info.block_count, disk.info.block_size, disk.info.max_transfer,
       disk.info.request_slots, disk.info.writable ? "writable with flush" : "read-only");
  for (;;) {
    unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
        VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    if (common_config()->device_status != expected ||
        common_config()->config_generation != disk.config_generation) {
      stop_transport("device status or configuration changed");
      return;
    }
    bool worked = false;
    const char *failure = complete_requests(&worked);
    uint64_t flags = cpu_save_interrupts();
    for (unsigned i = 0; !failure && i < disk.info.request_slots; ++i) {
      struct blk_slot *slot = &disk.slots[i];
      if (slot->state == BLK_ACTIVE && task_deadline_expired(slot->dma_deadline)) {
        failure = "device request timed out";
      }
    }
    cpu_restore_interrupts(flags);
    if (!failure) {
      failure = submit_requests(&worked);
    }
    if (failure) {
      stop_transport(failure);
      return;
    }
    if (worked) {
      kernel_task_sleep_until(arch_monotonic_ns());
    } else {
      wait_for_work();
    }
  }
}

void virtio_blk_start(void)
{
  if (!disk.prepared) {
    return;
  }
  enum mm_result result = kernel_task_create(block_worker, NULL);
  if (result != MM_OK) {
    klog("virtio-blk: cannot create worker (error %u); DMA disabled, resources retained\n",
         (unsigned)result);
    return;
  }
  disk.accepting = true;
}

void virtio_blk_prepare(const struct boot_info *boot)
{
  struct pci_device *device;
  enum pci_selection selection = pci_select_device(VIRTIO_VENDOR_ID,
      VIRTIO_PCI_DEVICE_BASE + VIRTIO_BLK_DEVICE_ID, &device);
  struct pci_device *transitional;
  enum pci_selection legacy = pci_select_device(VIRTIO_VENDOR_ID,
      VIRTIO_BLK_TRANSITIONAL_PCI_ID, &transitional);
  if (selection == PCI_SELECTION_INCOMPLETE || legacy == PCI_SELECTION_INCOMPLETE) {
    preparation = BLOCK_INVENTORY_INCOMPLETE;
    klog("virtio-blk: incomplete PCI inventory; block I/O unavailable\n");
    return;
  }
  if (selection == PCI_SELECTION_AMBIGUOUS || legacy == PCI_SELECTION_AMBIGUOUS ||
      (selection == PCI_SELECTION_UNIQUE && legacy == PCI_SELECTION_UNIQUE)) {
    preparation = BLOCK_DEVICE_AMBIGUOUS;
    klog("virtio-blk: multiple candidate devices; block I/O unavailable\n");
    return;
  }
  if (legacy == PCI_SELECTION_UNIQUE) {
    preparation = BLOCK_DEVICE_UNSUPPORTED;
    klog("virtio-blk: transitional device unsupported; block I/O unavailable\n");
    return;
  }
  if (selection != PCI_SELECTION_UNIQUE) {
    KASSERT(selection == PCI_SELECTION_ABSENT);
    preparation = BLOCK_DEVICE_ABSENT;
    klog("virtio-blk: no candidate device; block I/O unavailable\n");
    return;
  }
  preparation = BLOCK_DEVICE_SETUP_FAILED;
  disk.pci.name = "virtio-blk";
  if (!virtio_pci_prepare(&disk.pci, device, boot, sizeof(uint64_t), sizeof(uint32_t))) {
    return;
  }
  const char *failure = negotiate_transport();
  if (!failure && !prepare_msix()) {
    failure = "MSI-X routing rejected";
  }
  if (!failure && disk.queue_info.max_size < BLK_REQUEST_DESCRIPTORS) {
    failure = "insufficient queue descriptors";
  }
  if (!failure) {
    unsigned maximum = disk.queue_info.max_size;
    unsigned size = maximum < VIRTIO_BLK_QUEUE_SIZE ? maximum : VIRTIO_BLK_QUEUE_SIZE;
    disk.info.request_slots = size / BLK_REQUEST_DESCRIPTORS;
    if (disk.info.request_slots > BLK_SLOT_LIMIT) {
      disk.info.request_slots = BLK_SLOT_LIMIT;
    }
    enum mm_result result = virtqueue_allocate(&disk.queue, VIRTIO_BLK_QUEUE,
        size, maximum, disk.queue_info.notify_address);
    for (unsigned i = 0; result == MM_OK && i < disk.info.request_slots; ++i) {
      result = dma_buffer_allocate(&disk.slots[i].control, sizeof(struct blk_header) + 1);
      if (result == MM_OK) {
        result = dma_buffer_allocate(&disk.slots[i].data, disk.info.max_transfer);
      }
    }
    if (result != MM_OK || !configure_queue(&disk.queue)) {
      failure = "cannot allocate or configure block queue";
    }
  }
  unsigned expected = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (!failure && (common_config()->device_status != expected ||
      common_config()->config_generation != disk.config_generation)) {
    failure = "device changed during preparation";
  }
  if (failure) {
    bool interrupts_disabled = disable_msix();
    common_config()->device_status |= VIRTIO_STATUS_FAILED;
    bool reset = virtio_pci_reset(&disk.pci);
    klog("virtio-blk PCI: %s (reset=%u MSI-X disabled=%u)\n", failure,
         (unsigned)reset, (unsigned)interrupts_disabled);
    if (reset && interrupts_disabled) {
      for (unsigned i = 0; i < BLK_SLOT_LIMIT; ++i) {
        dma_buffer_release(&disk.slots[i].data);
        dma_buffer_release(&disk.slots[i].control);
      }
      virtqueue_release(&disk.queue);
      pci_release_device(&disk.pci.claim);
      disk = (typeof(disk)){0};
    }
    return;
  }
  disk.prepared = true;
  preparation = BLOCK_DEVICE_READY;
  klog("virtio-blk PCI: queue prepared; DMA and delivery disabled until worker start\n");
}
