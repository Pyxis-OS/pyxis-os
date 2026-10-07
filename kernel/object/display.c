#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object/display.h>
#include <kernel/display.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user_memory.h>

static void destroy_display(struct kernel_object *object)
{
  struct display_object *display = (struct display_object *)object;
  KASSERT(!display->owner && !display->frame);
  kfree(display);
}

struct display_object *display_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  struct display_object *display = kmalloc(sizeof(*display));
  if (!display) {
    return NULL;
  }
  *display = (struct display_object){.space = space};
  object_init(&display->object, OBJECT_DISPLAY, destroy_display);
  return display;
}

void display_frame_release(struct display_frame *frame)
{
  KASSERT(arch_cpu_index() == 0 && frame->references);
  if (--frame->references == 0) {
    struct execution_group *group = frame->cleanup_group;
    KASSERT(vm_free(vm_kernel_space(), frame->fb.address, frame->fb.size) == MM_OK);
    kfree(frame);
    if (group) {
      execution_group_cleanup_end(group);
    }
  }
}

struct display_frame *display_snapshot(struct display_object *display)
{
  KASSERT(arch_cpu_index() == 0);
  if (!display->visible) {
    return NULL;
  }
  struct display_frame *frame = display->frame;
  KASSERT(frame && frame->references && frame->references < SIZE_MAX);
  ++frame->references;
  return frame;
}

static void unmap_pixels(struct process *process, uintptr_t address, size_t size)
{
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(vm_unmap(process->address_space, address + offset, &physical) == MM_OK);
  }
}

static enum call_status acquire_display(struct display_object *display,
    struct process *process, struct display_buffer *reply)
{
  if (display->owner) {
    return CALL_BUSY;
  }
  const struct framebuffer *layout = display->space->fb;
  if (!layout->size || layout->size > SIZE_MAX - (PAGE_SIZE - 1)) {
    return CALL_LIMIT;
  }
  size_t mapping_size = (layout->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  struct display_frame *frame = kmalloc(sizeof(*frame));
  if (!frame) {
    return CALL_NO_MEMORY;
  }
  *frame = (struct display_frame){.fb = *layout, .references = 1};
  enum mm_result result = vm_alloc(vm_kernel_space(), mapping_size, PAGE_SIZE,
      PAGE_WRITE, &frame->fb.address);
  if (result != MM_OK) {
    kfree(frame);
    return CALL_NO_MEMORY;
  }

  uintptr_t address;
  result = vm_reserve(process->address_space, mapping_size, PAGE_SIZE, &address);
  if (result != MM_OK) {
    display_frame_release(frame);
    return CALL_NO_MEMORY;
  }
  size_t mapped = 0;
  for (; mapped < mapping_size; mapped += PAGE_SIZE) {
    struct page_translation page;
    KASSERT(vm_query(vm_kernel_space(), frame->fb.address + mapped, &page) == MM_OK);
    result = vm_map(process->address_space, address + mapped, page.physical,
        PAGE_USER | PAGE_WRITE);
    if (result != MM_OK) {
      unmap_pixels(process, address, mapped);
      KASSERT(vm_release(process->address_space, address, mapping_size) == MM_OK);
      display_frame_release(frame);
      return CALL_NO_MEMORY;
    }
  }

  display->frame = frame;
  display->owner = process;
  display->user_address = address;
  *reply = (struct display_buffer){
    .address = address,
    .size = mapping_size,
    .width = layout->width,
    .height = layout->height,
    .pitch = layout->pitch,
    .red_shift = layout->red_shift,
    .green_shift = layout->green_shift,
    .blue_shift = layout->blue_shift,
  };
  return CALL_OK;
}

static void release_display(struct display_object *display)
{
  struct display_frame *frame = display->frame;
  if (frame->references > 1) {
    struct execution_group *group = display->owner->execution_group;
    if (group) {
      execution_group_cleanup_begin(group);
      frame->cleanup_group = group;
    } else {
      frame->cleanup_group = object_cleanup_defer();
    }
  }
  size_t size = (frame->fb.size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  unmap_pixels(display->owner, display->user_address, size);
  KASSERT(vm_release(display->owner->address_space, display->user_address, size) == MM_OK);

  display->visible = false;
  display->owner = NULL;
  display->frame = NULL;
  display->user_address = 0;
  /* A preempted presenter may still read this frame. It owns its own reference
   * and will release the backing and pending cleanup after finishing. */
  display_frame_release(frame);
}

static enum call_status service_display(struct display_object *display,
    struct process *process, uint64_t operation, struct display_buffer *reply)
{
  KASSERT(arch_cpu_index() == 0);
  if (display->space != process->space) {
    return CALL_DENIED;
  }
  if (operation != DISPLAY_RELEASE && !display_available()) {
    return CALL_UNAVAILABLE;
  }
  if (operation == DISPLAY_ACQUIRE) {
    return acquire_display(display, process, reply);
  }
  if (display->owner != process) {
    return CALL_DENIED;
  }
  if (operation == DISPLAY_PRESENT) {
    display->visible = true;
  } else {
    KASSERT(operation == DISPLAY_RELEASE);
    release_display(display);
  }
  return CALL_OK;
}

void display_request_execute(struct display_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->loan && request->display);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  request->result = service_display(request->display, request->loan,
      request->operation, &request->reply);
  request->loan = NULL;
  request->display = NULL;
}

static enum call_status request_display(struct display_object *display,
    uint64_t operation, struct display_buffer *reply)
{
  struct display_request *request =
      (struct display_request *)bsp_request_prepare(BSP_SERVICE_DISPLAY);
  request->loan = process_current();
  KASSERT(request->loan);
  request->display = display;
  request->operation = operation;
  request->reply = (struct display_buffer){0};

  bsp_request_submit_and_wait(&request->request);
  *reply = request->reply;
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

void display_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  struct display_object *display = process->space->display;
  if (display->owner == process) {
    release_display(display);
  }
}

struct syscall_result display_call(struct display_object *display, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (operation != DISPLAY_ACQUIRE && operation != DISPLAY_PRESENT && operation != DISPLAY_RELEASE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & DISPLAY_RIGHT_DRAW)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct display_buffer reply = {0};
  if (operation == DISPLAY_ACQUIRE) {
    if (reply_capacity < sizeof(reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  }

  enum call_status status = request_display(display, operation, &reply);
  if (status != CALL_OK || operation != DISPLAY_ACQUIRE) {
    return (struct syscall_result){status, 0};
  }
  /* Acquisition adds a disjoint mapping; the checked reply stays writable. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
