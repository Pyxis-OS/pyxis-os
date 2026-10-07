#include "internal.h"
#include "virtio_gpu.h"
#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>
#include <kernel/virtio/queue.h>
#include <kernel/virtio/transport.h>

#define GPU_DEVICE_ID 16u
#define GPU_CONTROL_QUEUE 0u
#define GPU_CURSOR_QUEUE 1u
#define GPU_MSIX_ENTRY 0u
#define GPU_NO_VECTOR UINT16_MAX
#define GPU_QUEUE_SIZE 16u
#define GPU_CONFIG_BYTES 16u
#define GPU_RESOURCE_ID 1u
#define GPU_SCANOUT_COUNT 16u
#define GPU_COMMAND_TIMEOUT_MS 1000u
#define GPU_FORMAT_B8G8R8X8_UNORM 2u
#define GPU_FLAG_FENCE 1u
#define GPU_CMD_GET_DISPLAY_INFO 0x0100u
#define GPU_CMD_RESOURCE_CREATE_2D 0x0101u
#define GPU_CMD_SET_SCANOUT 0x0103u
#define GPU_CMD_RESOURCE_FLUSH 0x0104u
#define GPU_CMD_TRANSFER_TO_HOST_2D 0x0105u
#define GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u
#define GPU_RESP_OK_NODATA 0x1100u
#define GPU_RESP_OK_DISPLAY_INFO 0x1101u
#define GPU_PREPARED_STATUS (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | \
    VIRTIO_STATUS_FEATURES_OK)
#define GPU_ACTIVE_STATUS (GPU_PREPARED_STATUS | VIRTIO_STATUS_DRIVER_OK)

/* Minimal little-endian 2D wire layouts from VirtIO 1.4, section 5.7:
 * https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf
 * No context/ring-index feature is negotiated; their header bytes stay zero. */
struct gpu_header {
  uint32_t type, flags;
  uint64_t fence_id;
  uint32_t context_id, reserved;
};

struct gpu_rectangle {
  uint32_t x, y, width, height;
};

struct gpu_create {
  struct gpu_header header;
  uint32_t resource_id, format, width, height;
};

struct gpu_scanout {
  struct gpu_header header;
  struct gpu_rectangle rectangle;
  uint32_t scanout_id, resource_id;
};

struct gpu_flush {
  struct gpu_header header;
  struct gpu_rectangle rectangle;
  uint32_t resource_id, reserved;
};

struct gpu_transfer {
  struct gpu_header header;
  struct gpu_rectangle rectangle;
  uint64_t offset;
  uint32_t resource_id, reserved;
};

struct gpu_attach {
  struct gpu_header header;
  uint32_t resource_id, entry_count;
};

struct gpu_backing_entry {
  uint64_t physical;
  uint32_t bytes, reserved;
};

struct gpu_display_info {
  struct gpu_header header;
  struct {
    struct gpu_rectangle rectangle;
    uint32_t enabled, flags;
  } scanouts[GPU_SCANOUT_COUNT];
};

union gpu_command {
  struct gpu_header header;
  struct gpu_create create;
  struct gpu_scanout scanout;
  struct gpu_flush flush;
  struct gpu_transfer transfer;
};

_Static_assert(sizeof(struct gpu_header) == 24, "GPU control header layout");
_Static_assert(sizeof(struct gpu_create) == 40, "GPU create layout");
_Static_assert(sizeof(struct gpu_scanout) == 48, "GPU scanout layout");
_Static_assert(sizeof(struct gpu_flush) == 48, "GPU flush layout");
_Static_assert(sizeof(struct gpu_transfer) == 56, "GPU transfer layout");
_Static_assert(sizeof(struct gpu_attach) == 32, "GPU attach layout");
_Static_assert(sizeof(struct gpu_backing_entry) == 16, "GPU backing entry layout");
_Static_assert(sizeof(struct gpu_display_info) == 408, "GPU display info layout");
_Static_assert(sizeof(union gpu_command) == 56, "GPU command storage layout");

static struct {
  struct virtio_pci_transport pci;
  struct virtio_queue_info queue_info;
  struct virtqueue queue;
  struct dma_buffer control;
  struct framebuffer target;
  struct task_wait *wait;
  size_t backing_bytes, attach_bytes, command_offset, reply_offset;
  uint64_t fence_id;
  uint32_t scanout_id;
  const char *failure;
  bool prepared, active, started, bound, stopped;
  bool interrupt_ready, notified;
  bool boot_poll, geometry_queried;
} gpu;

static volatile struct virtio_pci_common *common_config(void)
{
  return virtio_pci_common(&gpu.pci);
}

static void assert_presenter_context(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

void virtio_gpu_interrupt(void)
{
  if (!gpu.interrupt_ready) {
    return;
  }
  gpu.notified = true;
  struct task_wait *wait = gpu.wait;
  gpu.wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

/* Panic revokes normal work without touching device registers or DMA storage. */
static void abandon_transport(void)
{
  uint64_t flags = cpu_save_interrupts();
  gpu.active = false;
  gpu.stopped = true;
  gpu.interrupt_ready = false;
  KASSERT(!gpu.wait);
  cpu_restore_interrupts(flags);
  virtqueue_stop(&gpu.queue);
}

static bool disable_dma(void)
{
  struct pci_claim *claim = &gpu.pci.claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  return !(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER);
}

static void stop_transport(const char *reason)
{
  if (display_is_panicking()) {
    abandon_transport();
    return;
  }
  virtqueue_stop(&gpu.queue);
  uint64_t flags = cpu_save_interrupts();
  gpu.active = false;
  gpu.stopped = true;
  gpu.interrupt_ready = false;
  KASSERT(!gpu.wait);
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return;
  }
  bool interrupts_disabled = virtio_pci_disable_msix(&gpu.pci);
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return;
  }
  bool dma_disabled = disable_dma();
  if (display_is_panicking()) {
    cpu_restore_interrupts(flags);
    return;
  }
  common_config()->device_status = 0;
  cpu_restore_interrupts(flags);

  uint64_t deadline = task_deadline_after_ms(VIRTIO_RESET_TIMEOUT_NS / UINT64_C(1000000));
  while (!display_is_panicking() && common_config()->device_status &&
      !task_deadline_expired(deadline)) {
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (display_is_panicking()) {
    return;
  }
  bool reset = common_config()->device_status == 0;
  if (reset) {
    virtqueue_confirm_reset(&gpu.queue);
  }
  /* Reset does not authorize runtime VM mutation or backing retirement. */
  ktrace("virtio-gpu: stopped reset=%u MSI-X disabled=%u DMA disabled=%u; storage retained\n",
      (unsigned)reset, (unsigned)interrupts_disabled, (unsigned)dma_disabled);
  klog("virtio-gpu: %s; display stopped\n", reason);
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
  common->driver_feature_select = 0;
  common->driver_feature = 0;
  common->driver_feature_select = 1;
  common->driver_feature = VIRTIO_F_VERSION_1 >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != GPU_PREPARED_STATUS) {
    return "feature negotiation rejected or device needs reset";
  }
  if (common->num_queues < 2 ||
      !virtio_pci_inspect_queue(&gpu.pci, GPU_CONTROL_QUEUE, &gpu.queue_info) ||
      gpu.queue_info.max_size < 2) {
    return "control queue requires at least two descriptors";
  }
  common->queue_select = GPU_CURSOR_QUEUE;
  if (common->queue_enable) {
    return "cursor queue did not remain disabled";
  }
  return common->device_status == GPU_PREPARED_STATUS ? NULL :
      "device status changed during preparation";
}

static bool prepare_msix(void)
{
  if (!virtio_pci_prepare_msix(&gpu.pci, APIC_VIRTIO_GPU_VECTOR)) {
    return false;
  }
  volatile struct virtio_pci_common *common = common_config();
  common->queue_select = GPU_CONTROL_QUEUE;
  common->queue_msix_vector = GPU_MSIX_ENTRY;
  return common->queue_msix_vector == GPU_MSIX_ENTRY;
}

static bool configure_queue(uint16_t vector)
{
  struct virtqueue *queue = &gpu.queue;
  volatile struct virtio_pci_common *common = common_config();
  common->queue_select = GPU_CONTROL_QUEUE;
  if (common->queue_enable) {
    return false;
  }
  common->queue_msix_vector = vector;
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
      common->queue_msix_vector != vector) {
    return false;
  }
  dma_write_barrier();
  common->queue_enable = 1;
  return common->queue_enable == 1 && common->device_status == GPU_PREPARED_STATUS;
}

static const char *allocate_backing(const struct boot_framebuffer *boot)
{
  if (!boot->width || !boot->height || boot->width > UINT32_MAX ||
      boot->height > UINT32_MAX || boot->width > SIZE_MAX / sizeof(uint32_t)) {
    return "invalid boot framebuffer dimensions";
  }
  size_t pitch = boot->width * sizeof(uint32_t);
  if (boot->height > SIZE_MAX / pitch) {
    return "framebuffer extent overflows";
  }
  size_t bytes = pitch * boot->height;
  if (bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return "framebuffer page extent overflows";
  }
  size_t extent = (bytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
  size_t pages = extent / PAGE_SIZE;
  if (pages > UINT32_MAX ||
      pages > (SIZE_MAX - sizeof(struct gpu_attach)) / sizeof(struct gpu_backing_entry)) {
    return "backing entry count overflows";
  }
  size_t attach_bytes = sizeof(struct gpu_attach) + pages * sizeof(struct gpu_backing_entry);
  size_t tail_bytes = sizeof(union gpu_command) + sizeof(struct gpu_display_info);
  if (attach_bytes > UINT32_MAX - sizeof(struct gpu_header) ||
      attach_bytes > SIZE_MAX - tail_bytes ||
      attach_bytes + tail_bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return "control DMA extent overflows";
  }

  uintptr_t address;
  if (vm_alloc(vm_kernel_space(), extent, PAGE_SIZE, PAGE_WRITE, &address) != MM_OK) {
    return "cannot allocate framebuffer backing";
  }
  gpu.backing_bytes = extent;
  gpu.target = (struct framebuffer){
    .address = address,
    .size = bytes,
    .pitch = pitch,
    .width = boot->width,
    .height = boot->height,
    .red_shift = 16,
    .green_shift = 8,
    .blue_shift = 0,
  };
  if (address > UINTPTR_MAX - (extent - 1) ||
      dma_buffer_allocate(&gpu.control, attach_bytes + tail_bytes) != MM_OK) {
    return "cannot allocate control DMA storage";
  }
  /* The attach request survives every reuse of the adjacent command/reply. */
  gpu.attach_bytes = attach_bytes;
  gpu.command_offset = attach_bytes;
  gpu.reply_offset = attach_bytes + sizeof(union gpu_command);
  struct gpu_attach *attach = (void *)gpu.control.address;
  *attach = (struct gpu_attach){
    .header.type = GPU_CMD_RESOURCE_ATTACH_BACKING,
    .resource_id = GPU_RESOURCE_ID,
    .entry_count = pages,
  };
  struct gpu_backing_entry *entries = (void *)(gpu.control.address + sizeof(*attach));
  for (size_t i = 0; i < pages; ++i) {
    struct page_translation page;
    size_t offset = i * PAGE_SIZE;
    size_t length = bytes - offset < PAGE_SIZE ? bytes - offset : PAGE_SIZE;
    if (vm_query(vm_kernel_space(), address + offset, &page) != MM_OK ||
        page.physical % PAGE_SIZE || page.physical > UINT64_MAX - (length - 1) ||
        !(page.permissions & PAGE_WRITE) || (page.permissions & PAGE_USER)) {
      return "cannot resolve framebuffer backing pages";
    }
    entries[i] = (struct gpu_backing_entry){.physical = page.physical, .bytes = length};
  }
  return NULL;
}

static void unwind_preparation(const char *reason)
{
  gpu.active = false;
  gpu.interrupt_ready = false;
  gpu.stopped = true;
  if (display_is_panicking()) {
    return;
  }
  bool interrupts_disabled = virtio_pci_disable_msix(&gpu.pci);
  bool dma_disabled = disable_dma();
  bool reset = virtio_pci_reset(&gpu.pci);
  ktrace("virtio-gpu: preparation failed reset=%u MSI-X disabled=%u DMA disabled=%u\n",
      (unsigned)reset, (unsigned)interrupts_disabled, (unsigned)dma_disabled);
  if (reset && interrupts_disabled && dma_disabled) {
    virtqueue_confirm_reset(&gpu.queue);
    dma_buffer_release(&gpu.control);
    virtqueue_release(&gpu.queue);
    if (gpu.target.address) {
      KASSERT(vm_free(vm_kernel_space(), gpu.target.address, gpu.backing_bytes) == MM_OK);
      gpu.target = (struct framebuffer){0};
      gpu.backing_bytes = 0;
    }
    /* Bootstrap DMA permanently excludes PCI boot release, even after reset.
     * Keep the claim and mapping records at their original stable addresses. */
    if (!gpu.pci.claim.dma_started) {
      pci_release_device(&gpu.pci.claim);
      gpu = (typeof(gpu)){.stopped = true};
    }
  }
  klog("virtio-gpu: %s; unavailable\n", reason);
}

static const char *query_boot_geometry(struct boot_framebuffer *geometry);

bool virtio_gpu_matches(const struct pci_device *device)
{
  return device->vendor_id == VIRTIO_VENDOR_ID &&
      device->device_id == VIRTIO_PCI_DEVICE_BASE + GPU_DEVICE_ID;
}

const struct framebuffer *virtio_gpu_prepare(const struct boot_info *boot,
    struct pci_device *device)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(device && virtio_gpu_matches(device));
  gpu.pci.name = "virtio-gpu";
  gpu.pci.trace_details = true;
  if (!virtio_pci_prepare(&gpu.pci, device, boot, GPU_CONFIG_BYTES, 4)) {
    gpu.stopped = true;
    klog("virtio-gpu: PCI transport unavailable\n");
    return NULL;
  }
  const char *failure = negotiate_transport();
  if (!failure && !prepare_msix()) {
    failure = "MSI-X routing rejected";
  }
  struct boot_framebuffer geometry = boot->framebuffer;
  if (!failure && (!geometry.address || !geometry.size || !geometry.width)) {
    failure = query_boot_geometry(&geometry);
    if (!failure) {
      failure = negotiate_transport();
    }
  }
  if (!failure) {
    volatile struct virtio_pci_common *common = common_config();
    common->config_msix_vector = GPU_MSIX_ENTRY;
    if (common->config_msix_vector != GPU_MSIX_ENTRY) {
      failure = "MSI-X configuration route rejected";
    }
  }
  if (!failure) {
    unsigned maximum = gpu.queue_info.max_size;
    unsigned size = maximum < GPU_QUEUE_SIZE ? maximum : GPU_QUEUE_SIZE;
    if (virtqueue_allocate(&gpu.queue, GPU_CONTROL_QUEUE, size, maximum,
        gpu.queue_info.notify_address) != MM_OK) {
      failure = "cannot allocate control queue";
    }
  }
  if (!failure) {
    failure = allocate_backing(&geometry);
  }
  if (!failure && !configure_queue(GPU_MSIX_ENTRY)) {
    failure = "cannot configure control queue";
  }
  if (failure) {
    unwind_preparation(failure);
    return NULL;
  }
  gpu.prepared = true;
  ktrace("virtio-gpu: queue=%u backing=%zu bytes attach=%zu bytes; prepared with normal DMA disabled\n",
      (unsigned)gpu.queue.size, gpu.target.size, gpu.attach_bytes);
  return &gpu.target;
}

static union gpu_command *prepare_command(uint32_t type)
{
  KASSERT(!gpu.queue.outstanding);
  union gpu_command *command = (void *)(gpu.control.address + gpu.command_offset);
  memset(command, 0, sizeof(*command));
  command->header.type = type;
  return command;
}

static bool control_request(size_t offset, size_t bytes, uint32_t response_type,
    size_t response_bytes)
{
  KASSERT(gpu.active && !gpu.stopped && !gpu.queue.outstanding);
  if (display_is_panicking()) {
    abandon_transport();
    return false;
  }
  if (gpu.fence_id == UINT64_MAX) {
    gpu.failure = "control fence IDs exhausted";
    return false;
  }
  struct gpu_header *request = (void *)(gpu.control.address + offset);
  uint64_t fence_id = ++gpu.fence_id;
  request->flags = GPU_FLAG_FENCE;
  request->fence_id = fence_id;
  request->context_id = 0;
  request->reserved = 0;
  struct gpu_header *response = (void *)(gpu.control.address + gpu.reply_offset);
  memset(response, 0, sizeof(struct gpu_display_info));
  struct virtqueue_segment segments[] = {
    {.physical = gpu.control.physical + offset, .bytes = bytes,
     .access = VIRTQUEUE_DEVICE_READ},
    {.physical = gpu.control.physical + gpu.reply_offset, .bytes = response_bytes,
     .access = VIRTQUEUE_DEVICE_WRITE},
  };
  uint64_t deadline = task_deadline_after_ms(GPU_COMMAND_TIMEOUT_MS);
  uint64_t flags = cpu_save_interrupts();
  gpu.notified = false;
  cpu_restore_interrupts(flags);
  if (display_is_panicking()) {
    abandon_transport();
    return false;
  }
  if (virtqueue_submit(&gpu.queue, segments, 2, fence_id) != VIRTQUEUE_ACCEPTED) {
    gpu.failure = "control request publication failed";
    return false;
  }
  if (display_is_panicking()) {
    abandon_transport();
    return false;
  }
  virtqueue_notify(&gpu.queue);

  for (;;) {
    if (display_is_panicking()) {
      abandon_transport();
      return false;
    }
    if (common_config()->device_status != GPU_ACTIVE_STATUS) {
      gpu.failure = "device status changed during control request";
      return false;
    }
    struct virtqueue_completion completion;
    size_t count;
    enum virtqueue_result result = virtqueue_complete(&gpu.queue, &completion, 1, &count);
    if (result == VIRTQUEUE_COMPLETE) {
      if (count != 1 || completion.request_id != fence_id ||
          completion.written != response_bytes || response->type != response_type ||
          response->flags != GPU_FLAG_FENCE || response->fence_id != fence_id ||
          response->context_id || response->reserved) {
        ktrace("virtio-gpu: command=0x%x fence=%lu response=0x%x flags=0x%x fence=%lu context=%u bytes=%u\n",
            request->type, fence_id, response->type, response->flags,
            response->fence_id, response->context_id, completion.written);
        gpu.failure = "invalid fenced control response";
        return false;
      }
      return true;
    }
    if (result != VIRTQUEUE_PENDING) {
      gpu.failure = "control queue completion failed";
      return false;
    }
    if (task_deadline_expired(deadline)) {
      gpu.failure = "control request timed out";
      return false;
    }
    /* Only the temporary display-info queue runs before tasks/APs exist.
     * Its IRQ routes stay disabled; the normal presenter always sleeps. */
    if (gpu.boot_poll) {
      arch_clock_maintain();
      __asm__ volatile("pause");
      continue;
    }

    flags = cpu_save_interrupts();
    if (display_is_panicking()) {
      cpu_restore_interrupts(flags);
      abandon_transport();
      return false;
    }
    if (gpu.notified) {
      gpu.notified = false;
      cpu_restore_interrupts(flags);
      continue;
    }
    KASSERT(!gpu.wait);
    struct task_wait *wait = task_wait_prepare();
    gpu.wait = wait;
    task_wait_sleep_until(wait, deadline);
    if (gpu.wait == wait) {
      gpu.wait = NULL;
    }
    gpu.notified = false;
    cpu_restore_interrupts(flags);
  }
}

static bool activate_transport(void)
{
  uint64_t flags = cpu_save_interrupts();
  volatile struct virtio_pci_common *common = common_config();
  bool ready = !display_is_panicking() && common->device_status == GPU_PREPARED_STATUS;
  if (ready) {
    struct pci_claim *claim = &gpu.pci.claim;
    uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
    pci_write16(claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
    ready = (pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  }
  if (ready && !display_is_panicking()) {
    common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    ready = common->device_status == GPU_ACTIVE_STATUS;
  } else {
    ready = false;
  }
  if (ready && !display_is_panicking()) {
    gpu.active = true;
    if (!gpu.boot_poll) {
      gpu.interrupt_ready = true;
      ready = pci_msix_enable(&gpu.pci.msix);
    }
  } else {
    ready = false;
  }
  cpu_restore_interrupts(flags);
  return ready;
}

static bool select_scanout(const struct gpu_display_info *info)
{
  if (!gpu.geometry_queried) {
    unsigned scanout;
    for (scanout = 0; scanout < GPU_SCANOUT_COUNT; ++scanout) {
      if (info->scanouts[scanout].enabled &&
          info->scanouts[scanout].rectangle.width &&
          info->scanouts[scanout].rectangle.height) {
        break;
      }
    }
    if (scanout == GPU_SCANOUT_COUNT) {
      return false;
    }
    gpu.scanout_id = scanout;
  }
  return info->scanouts[gpu.scanout_id].enabled &&
      info->scanouts[gpu.scanout_id].rectangle.width &&
      info->scanouts[gpu.scanout_id].rectangle.height;
}

static const char *query_boot_geometry(struct boot_framebuffer *geometry)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  gpu.boot_poll = true;
  /* MSI-X was prepared before the first DMA use and remains function/entry
   * masked. The temporary queue and configuration have no vector assigned. */
  unsigned maximum = gpu.queue_info.max_size;
  unsigned size = maximum < GPU_QUEUE_SIZE ? maximum : GPU_QUEUE_SIZE;
  if (virtqueue_allocate(&gpu.queue, GPU_CONTROL_QUEUE, size, maximum,
      gpu.queue_info.notify_address) != MM_OK ||
      dma_buffer_allocate(&gpu.control,
          sizeof(union gpu_command) + sizeof(struct gpu_display_info)) != MM_OK) {
    return "cannot allocate bootstrap display-info queue";
  }
  gpu.command_offset = 0;
  gpu.reply_offset = sizeof(union gpu_command);
  volatile struct virtio_pci_common *common = common_config();
  common->config_msix_vector = GPU_NO_VECTOR;
  if (common->config_msix_vector != GPU_NO_VECTOR ||
      !configure_queue(GPU_NO_VECTOR) || !activate_transport()) {
    return "cannot activate bootstrap display-info queue";
  }
  prepare_command(GPU_CMD_GET_DISPLAY_INFO);
  if (!control_request(0, sizeof(struct gpu_header), GPU_RESP_OK_DISPLAY_INFO,
      sizeof(struct gpu_display_info))) {
    return gpu.failure ? gpu.failure : "bootstrap display-info query abandoned";
  }
  const struct gpu_display_info *info = (const void *)(gpu.control.address + gpu.reply_offset);
  if (!select_scanout(info)) {
    return "no usable enabled scanout";
  }
  geometry->width = info->scanouts[gpu.scanout_id].rectangle.width;
  geometry->height = info->scanouts[gpu.scanout_id].rectangle.height;
  gpu.geometry_queried = true;

  virtqueue_stop(&gpu.queue);
  gpu.active = false;
  if (display_is_panicking()) {
    return "bootstrap display-info query abandoned";
  }
  bool dma_disabled = disable_dma();
  bool reset = virtio_pci_reset(&gpu.pci);
  if (!reset || !dma_disabled) {
    return "cannot stop bootstrap display-info queue";
  }
  virtqueue_confirm_reset(&gpu.queue);
  dma_buffer_release(&gpu.control);
  virtqueue_release(&gpu.queue);
  gpu.command_offset = 0;
  gpu.reply_offset = 0;
  gpu.boot_poll = false;
  ktrace("virtio-gpu: bootstrap scanout=%u reports %zux%zu; temporary DMA retired\n",
      gpu.scanout_id, geometry->width, geometry->height);
  return NULL;
}

bool virtio_gpu_start(void)
{
  assert_presenter_context();
  if (!gpu.prepared || gpu.stopped) {
    return false;
  }
  KASSERT(!gpu.active && !gpu.started);
  if (!activate_transport()) {
    stop_transport("transport activation failed");
    return false;
  }
  prepare_command(GPU_CMD_GET_DISPLAY_INFO);
  if (!control_request(gpu.command_offset, sizeof(struct gpu_header),
      GPU_RESP_OK_DISPLAY_INFO, sizeof(struct gpu_display_info))) {
    stop_transport(gpu.failure);
    return false;
  }
  const struct gpu_display_info *info = (const void *)(gpu.control.address + gpu.reply_offset);
  if (!select_scanout(info)) {
    stop_transport("no usable enabled scanout");
    return false;
  }
  union gpu_command *command = prepare_command(GPU_CMD_RESOURCE_CREATE_2D);
  command->create.resource_id = GPU_RESOURCE_ID;
  command->create.format = GPU_FORMAT_B8G8R8X8_UNORM;
  command->create.width = gpu.target.width;
  command->create.height = gpu.target.height;
  if (!control_request(gpu.command_offset, sizeof(command->create),
      GPU_RESP_OK_NODATA, sizeof(struct gpu_header)) ||
      !control_request(0, gpu.attach_bytes, GPU_RESP_OK_NODATA, sizeof(struct gpu_header))) {
    stop_transport(gpu.failure);
    return false;
  }
  gpu.started = true;
  ktrace("virtio-gpu: scanout=%u resource=%u at %zux%zu, backing attached\n",
      gpu.scanout_id, GPU_RESOURCE_ID, gpu.target.width, gpu.target.height);
  return true;
}

void virtio_gpu_copy(size_t offset, const void *pixels, size_t bytes)
{
  KASSERT(gpu.prepared && !gpu.queue.outstanding);
  KASSERT(offset <= gpu.target.size && bytes <= gpu.target.size - offset);
  memcpy((void *)(gpu.target.address + offset), pixels, bytes);
}

bool virtio_gpu_present(void)
{
  assert_presenter_context();
  if (!gpu.started || gpu.stopped) {
    return false;
  }
  struct gpu_rectangle rectangle = {
    .width = gpu.target.width,
    .height = gpu.target.height,
  };
  union gpu_command *command = prepare_command(GPU_CMD_TRANSFER_TO_HOST_2D);
  command->transfer.rectangle = rectangle;
  command->transfer.resource_id = GPU_RESOURCE_ID;
  if (!control_request(gpu.command_offset, sizeof(command->transfer),
      GPU_RESP_OK_NODATA, sizeof(struct gpu_header))) {
    stop_transport(gpu.failure);
    return false;
  }
  if (!gpu.bound) {
    command = prepare_command(GPU_CMD_SET_SCANOUT);
    command->scanout.rectangle = rectangle;
    command->scanout.scanout_id = gpu.scanout_id;
    command->scanout.resource_id = GPU_RESOURCE_ID;
    if (!control_request(gpu.command_offset, sizeof(command->scanout),
        GPU_RESP_OK_NODATA, sizeof(struct gpu_header))) {
      stop_transport(gpu.failure);
      return false;
    }
    gpu.bound = true;
  }
  command = prepare_command(GPU_CMD_RESOURCE_FLUSH);
  command->flush.rectangle = rectangle;
  command->flush.resource_id = GPU_RESOURCE_ID;
  if (!control_request(gpu.command_offset, sizeof(command->flush),
      GPU_RESP_OK_NODATA, sizeof(struct gpu_header))) {
    stop_transport(gpu.failure);
    return false;
  }
  return true;
}
