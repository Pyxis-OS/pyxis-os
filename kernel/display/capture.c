#include <kernel/object/forwarder.h>
#include <abi/file.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/display.h>
#include <kernel/display_capture.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/file.h>
#include <kernel/object/screen_capture.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>

/* BSP/IF=0 owns admission. The sole presenter owns active during IF=1 copying;
 * a preempting executor can inspect the occupied slot but never its pixels. */
static struct screen_capture_request *pending, *active;
static struct file_object *snapshot_file;
static struct capability_grant snapshot_grant;
static struct framebuffer frame_layout;
static void *frame_pixels;

static void complete_capture(struct screen_capture_request *request,
    enum call_status status)
{
  KASSERT(arch_cpu_index() == 0 && request);
  capability_reservation_release(&request->reservation, &request->slot);
  request->status = status;
  if (status != CALL_OK) {
    request->reply = (struct screen_capture_reply){0};
  }
  bsp_request_complete(&request->request);
}

void screen_capture_submit(struct screen_capture_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->request.state == BSP_REQUEST_FORWARDED);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  enum call_status status;
  if (forwarder_operation_denied(request->request.operation)) {
    status = CALL_DENIED;
  } else if (task_wait_stop_requested(request->request.wait)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (!display_available()) {
    status = CALL_UNAVAILABLE;
  } else if (pending || active) {
    status = CALL_BUSY;
  } else {
    pending = request;
    return;
  }
  complete_capture(request, status);
}

bool screen_capture_pending(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return pending != NULL;
}

void screen_capture_begin(const struct framebuffer *layout, uint64_t generation)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && !active && !frame_pixels);
  struct screen_capture_request *request = pending;
  if (!request) {
    cpu_restore_interrupts(flags);
    return;
  }
  pending = NULL;
  enum call_status status = CALL_OK;
  size_t pitch = 0, size = 0;
  if (task_wait_stop_requested(request->request.wait)) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (!display_available()) {
    status = CALL_UNAVAILABLE;
  } else if (!generation || !layout->width || !layout->height ||
      layout->width > SIZE_MAX / sizeof(uint32_t)) {
    status = CALL_LIMIT;
  } else {
    pitch = layout->width * sizeof(uint32_t);
    if (layout->height > SIZE_MAX / pitch || layout->pitch < pitch ||
        layout->height > layout->size / layout->pitch) {
      status = CALL_LIMIT;
    } else {
      size = pitch * layout->height;
      frame_pixels = kmalloc(size);
      if (!frame_pixels) {
        status = CALL_NO_MEMORY;
      }
    }
  }
  if (status == CALL_OK) {
    snapshot_file = file_prepare_snapshot(size);
    if (!snapshot_file) {
      status = CALL_NO_MEMORY;
    } else {
      enum capability_result result = capability_grant_retain(&snapshot_file->object,
          FILE_RIGHT_READ, 0, &snapshot_grant);
      if (result == CAP_OK) {
        result = forwarder_derive_grant(&snapshot_grant, request->request.operation);
      }
      if (result == CAP_OK) {
        result = capability_validate_grants(request->reservation.table, &snapshot_grant, 1);
      }
      status = result == CAP_OK ? CALL_OK : result == CAP_NO_MEMORY ? CALL_NO_MEMORY :
          result == CAP_DENIED ? CALL_DENIED : CALL_LIMIT;
    }
  }
  if (status == CALL_OK && !forwarder_operation_start(request->request.operation)) {
    status = CALL_DENIED;
    if (frame_pixels) {
      kfree(frame_pixels);
      frame_pixels = NULL;
    }
  }
  if (status == CALL_OK) {
    frame_layout = *layout;
    request->reply = (struct screen_capture_reply){
      .size = size, .width = layout->width, .height = layout->height,
      .pitch = pitch, .generation = generation,
      .red_shift = layout->red_shift, .green_shift = layout->green_shift,
      .blue_shift = layout->blue_shift,
    };
    active = request;
  } else {
    capability_grant_release(&snapshot_grant);
    if (snapshot_file) {
      object_release(&snapshot_file->object);
      snapshot_file = NULL;
    }
    if (frame_pixels) {
      kfree(frame_pixels);
      frame_pixels = NULL;
    }
    complete_capture(request, status);
  }
  cpu_restore_interrupts(flags);
}

bool screen_capture_active(void)
{
  return frame_pixels != NULL;
}

void screen_capture_read(size_t offset, void *pixels, size_t bytes)
{
  KASSERT(active && frame_pixels);
  KASSERT(offset <= frame_layout.size && bytes <= frame_layout.size - offset);
  KASSERT(!(offset % sizeof(uint32_t)) && !(bytes % sizeof(uint32_t)));
  size_t row = offset / frame_layout.pitch;
  size_t column = offset % frame_layout.pitch;
  size_t pitch = active->reply.pitch;
  KASSERT(row < frame_layout.height && column < pitch && bytes <= pitch - column);
  const uint8_t *staged = (const uint8_t *)frame_pixels + row * pitch + column;
  memcpy(pixels, staged, bytes);
}

void screen_capture_copy_only(size_t offset, const void *pixels, size_t bytes)
{
  KASSERT(active && frame_pixels);
  KASSERT(offset <= frame_layout.size && bytes <= frame_layout.size - offset);
  const uint8_t *source = pixels;
  size_t pitch = active->reply.pitch;
  while (bytes) {
    size_t row = offset / frame_layout.pitch;
    size_t column = offset % frame_layout.pitch;
    size_t count = MIN(bytes, frame_layout.pitch - column);
    if (row < frame_layout.height && column < pitch) {
      count = MIN(count, pitch - column);
      uint8_t *staged = (uint8_t *)frame_pixels + row * pitch + column;
      memcpy(staged, source, count);
    }
    offset += count;
    source += count;
    bytes -= count;
  }
}

void screen_capture_copy(size_t offset, const void *pixels, size_t bytes)
{
  if (!frame_pixels) {
    display_copy(offset, pixels, bytes);
    return;
  }
  KASSERT(offset <= frame_layout.size && bytes <= frame_layout.size - offset);
  const uint8_t *source = pixels;
  size_t pitch = active->reply.pitch;
  while (bytes) {
    size_t row = offset / frame_layout.pitch;
    size_t column = offset % frame_layout.pitch;
    size_t count = MIN(bytes, frame_layout.pitch - column);
    if (row < frame_layout.height && column < pitch) {
      count = MIN(count, pitch - column);
      uint8_t *staged = (uint8_t *)frame_pixels + row * pitch + column;
      memcpy(staged, source, count);
      display_copy(offset, staged, count);
    } else {
      display_copy(offset, source, count);
    }
    offset += count;
    source += count;
    bytes -= count;
  }
}

void screen_capture_finish(bool presented)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0);
  struct screen_capture_request *request = active;
  if (!request) {
    if (!presented && pending) {
      request = pending;
      pending = NULL;
      complete_capture(request, CALL_UNAVAILABLE);
    }
    cpu_restore_interrupts(flags);
    return;
  }
  active = NULL;
  void *pixels = frame_pixels;
  frame_pixels = NULL;
  struct execution_group *previous = object_cleanup_enter(request->request.cleanup_group);
  struct file_object *file = snapshot_file;
  snapshot_file = NULL;
  enum call_status status;
  if (!presented || !display_available()) {
    status = CALL_UNAVAILABLE;
  } else if (task_wait_stop_requested(request->request.wait)) {
    status = CALL_ENDPOINT_CLOSED;
  } else {
    /* File storage, derivation and the destination claim preceded capture. */
    file_fill_snapshot(file, pixels);
    capability_install_reserved(&request->reservation, &request->slot, &snapshot_grant, 1,
        &request->reply.file);
    status = CALL_OK;
  }
  capability_grant_release(&snapshot_grant);
  if (pixels) {
    kfree(pixels);
  }
  if (file) {
    object_release(&file->object);
  }
  object_cleanup_leave(previous);
  complete_capture(request, status);
  cpu_restore_interrupts(flags);
}
