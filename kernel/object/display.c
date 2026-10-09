#include <kernel/object/clipboard.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object/display.h>
#include <kernel/pointer.h>
#include <kernel/display.h>
#include <kernel/log.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/user_memory.h>

static void destroy_display(struct kernel_object *object)
{
  struct display_object *display = (struct display_object *)object;
  KASSERT(!display->owner && !display->current);
  kfree(display);
}

struct display_object *display_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  struct display_object *display = kmalloc(sizeof(*display));
  if (!display) {
    return NULL;
  }
  *display = (struct display_object){.space = space, .pending = DISPLAY_SLOT_COUNT};
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

static void retain_frame(struct display_frame *frame)
{
  KASSERT(frame->references && frame->references < SIZE_MAX);
  ++frame->references;
}

struct display_frame *display_snapshot(struct display_object *display)
{
  KASSERT(arch_cpu_index() == 0);
  if (!display->visible) {
    return NULL;
  }
  /* The previous current slot returns to the program by no longer being
   * current; nothing reads it after this presentation's own reference. */
  if (display->pending != DISPLAY_SLOT_COUNT) {
    struct display_frame *frame = display->slots[display->pending];
    display->pending = DISPLAY_SLOT_COUNT;
    retain_frame(frame);
    if (display->current) {
      display_frame_release(display->current);
    }
    display->current = frame;
  }
  struct display_frame *frame = display->current;
  KASSERT(frame);
  retain_frame(frame);
  return frame;
}

const struct framebuffer *display_slot_layout(const struct display_object *display)
{
  return display->owner ? &display->slots[0]->fb : NULL;
}

static void unmap_pixels(struct process *process, uintptr_t address, size_t size)
{
  for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(vm_unmap(process->address_space, address + offset, &physical) == MM_OK);
  }
}

static size_t slot_size(const struct framebuffer *layout)
{
  return (layout->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static struct display_frame *allocate_frame(const struct framebuffer *layout)
{
  struct display_frame *frame = kmalloc(sizeof(*frame));
  if (!frame) {
    return NULL;
  }
  *frame = (struct display_frame){.fb = *layout, .references = 1};
  if (vm_alloc(vm_kernel_space(), slot_size(layout), PAGE_SIZE, PAGE_WRITE,
      &frame->fb.address) != MM_OK) {
    kfree(frame);
    return NULL;
  }
  return frame;
}

static enum call_status map_frame(struct process *process, const struct display_frame *frame,
    uintptr_t address)
{
  size_t size = slot_size(&frame->fb);
  for (size_t mapped = 0; mapped < size; mapped += PAGE_SIZE) {
    struct page_translation page;
    KASSERT(vm_query(vm_kernel_space(), frame->fb.address + mapped, &page) == MM_OK);
    if (vm_map(process->address_space, address + mapped, page.physical,
        PAGE_USER | PAGE_WRITE) != MM_OK) {
      unmap_pixels(process, address, mapped);
      return CALL_NO_MEMORY;
    }
  }
  return CALL_OK;
}

/* Allocates and maps one zeroed slot set at a disjoint user range. Failure
 * leaves nothing allocated or mapped. */
static enum call_status prepare_slots(struct display_object *display,
    struct process *process, struct display_frame *slots[DISPLAY_SLOT_COUNT],
    struct display_buffer *reply)
{
  const struct framebuffer *layout = display->space->fb;
  if (!layout->size || layout->size > SIZE_MAX - (PAGE_SIZE - 1) ||
      slot_size(layout) > SIZE_MAX / DISPLAY_SLOT_COUNT) {
    return CALL_LIMIT;
  }
  size_t size = slot_size(layout);
  size_t prepared = 0;
  for (; prepared < DISPLAY_SLOT_COUNT; ++prepared) {
    slots[prepared] = allocate_frame(layout);
    if (!slots[prepared]) {
      break;
    }
  }
  uintptr_t address = 0;
  enum call_status status = prepared == DISPLAY_SLOT_COUNT ? CALL_OK : CALL_NO_MEMORY;
  if (status == CALL_OK && vm_reserve(process->address_space, size * DISPLAY_SLOT_COUNT,
      PAGE_SIZE, &address) != MM_OK) {
    status = CALL_NO_MEMORY;
  }
  size_t mapped = 0;
  for (; status == CALL_OK && mapped < DISPLAY_SLOT_COUNT; ++mapped) {
    status = map_frame(process, slots[mapped], address + mapped * size);
  }
  if (status != CALL_OK) {
    if (address) {
      /* map_frame already unmapped the slot that failed. */
      for (size_t i = 0; i + 1 < mapped; ++i) {
        unmap_pixels(process, address + i * size, size);
      }
      KASSERT(vm_release(process->address_space, address, size * DISPLAY_SLOT_COUNT) == MM_OK);
    }
    while (prepared) {
      display_frame_release(slots[--prepared]);
    }
    return status;
  }

  *reply = (struct display_buffer){
    .address = address,
    .size = size,
    .width = layout->width,
    .height = layout->height,
    .pitch = layout->pitch,
    .generation = display->space->tty->geometry_generation,
    .red_shift = layout->red_shift,
    .green_shift = layout->green_shift,
    .blue_shift = layout->blue_shift,
    .slot_count = DISPLAY_SLOT_COUNT,
  };
  return CALL_OK;
}

static enum call_status acquire_display(struct display_object *display,
    struct process *process, struct display_buffer *reply)
{
  if (display->owner) {
    return CALL_BUSY;
  }
  if (display->mapping_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  enum call_status status = prepare_slots(display, process, display->slots, reply);
  if (status != CALL_OK) {
    return status;
  }
  display->pending = DISPLAY_SLOT_COUNT;
  clipboard_space_cancel(display->space);
  ++display->mapping_identity;
  display->owner = process;
  display->user_address = reply->address;
  return CALL_OK;
}

/* A preempted presenter, or the session's on-screen reference, may still hold
 * a retired frame. The last holder releases the backing and pending cleanup. */
static void retire_frame(struct process *process, struct display_frame *frame)
{
  if (frame->references > 1 && !frame->cleanup_group) {
    struct execution_group *group = process->execution_group;
    if (group) {
      execution_group_cleanup_begin(group);
      frame->cleanup_group = group;
    } else {
      frame->cleanup_group = object_cleanup_defer();
    }
  }
  display_frame_release(frame);
}

/* Unmaps every slot and drops the session's slot references. The on-screen
 * reference is the caller's to keep or release. */
static void retire_slots(struct display_object *display, struct process *process)
{
  size_t size = slot_size(&display->slots[0]->fb);
  for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
    unmap_pixels(process, display->user_address + i * size, size);
  }
  KASSERT(vm_release(process->address_space, display->user_address,
      size * DISPLAY_SLOT_COUNT) == MM_OK);
  for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
    retire_frame(process, display->slots[i]);
    display->slots[i] = NULL;
  }
  display->pending = DISPLAY_SLOT_COUNT;
}

static void release_display(struct display_object *display)
{
  clipboard_space_cancel(display->space);
  pointer_surface_ended(display->space);
  struct display_frame *current = display->current;
  display->current = NULL;
  retire_slots(display, display->owner);
  if (current) {
    retire_frame(display->owner, current);
  }
  display->presented = false;
  display->visible = false;
  display->owner = NULL;
  display->user_address = 0;
  space_display_changed(display->space, false);
}

void display_select_layer(struct display_object *display, bool graphics)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!display->presented || display->visible == graphics) {
    return;
  }
  clipboard_space_cancel(display->space);
  display->visible = graphics;
  space_display_changed(display->space, graphics);
}

static enum call_status replace_display(struct display_object *display,
    struct process *process, uint64_t generation, uintptr_t reply_address,
    struct display_buffer *reply)
{
  if (display->mapping_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  if (generation != display->space->tty->geometry_generation) {
    return CALL_BUSY;
  }
  size_t size = slot_size(&display->slots[0]->fb) * DISPLAY_SLOT_COUNT;
  if (reply_address < display->user_address + size &&
      display->user_address < reply_address + sizeof(*reply)) {
    return CALL_BAD_BUFFER;
  }

  struct display_frame *slots[DISPLAY_SLOT_COUNT];
  enum call_status status = prepare_slots(display, process, slots, reply);
  if (status != CALL_OK) {
    return status;
  }
  /* The frame on screen keeps its own reference until a new frame replaces it. */
  retire_slots(display, process);
  for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
    display->slots[i] = slots[i];
  }
  display->user_address = reply->address;
  clipboard_space_cancel(display->space);
  ++display->mapping_identity;
  pointer_geometry_changed(display->space);
  return CALL_OK;
}

static enum call_status submit_display(struct display_object *display, uint64_t slot,
    struct display_submit_reply *reply)
{
  if (slot >= DISPLAY_SLOT_COUNT || slot == display->pending ||
      display->slots[slot] == display->current) {
    return CALL_BAD_REQUEST;
  }
  bool dropped = display->pending != DISPLAY_SLOT_COUNT;
  display->pending = slot;
  size_t next = 0;
  while (next == display->pending || display->slots[next] == display->current) {
    ++next;
  }
  KASSERT(next < DISPLAY_SLOT_COUNT);
  *reply = (struct display_submit_reply){.next = next, .dropped = dropped};
  if (!display->presented) {
    display->presented = true;
    display->visible = true;
    space_display_changed(display->space, false);
  }
  return CALL_OK;
}

static enum call_status service_display(struct display_object *display,
    struct process *process, uint64_t operation, uint64_t generation, uint64_t slot,
    uintptr_t reply_address, union display_reply *reply)
{
  KASSERT(arch_cpu_index() == 0);
  if (display->space != process->space) {
    return CALL_DENIED;
  }
  if (operation != DISPLAY_RELEASE && !display_available()) {
    return CALL_UNAVAILABLE;
  }
  if (operation == DISPLAY_ACQUIRE) {
    return acquire_display(display, process, &reply->buffer);
  }
  if (operation == DISPLAY_SIZE) {
    bool locked = log_begin();
    if (!locked) {
      return CALL_UNAVAILABLE;
    }
    const struct framebuffer *layout = display->space->fb;
    reply->size = (struct display_size_reply){
      .width = layout->width,
      .height = layout->height,
      .pitch = layout->pitch,
      .generation = display->space->tty->geometry_generation,
      .red_shift = layout->red_shift,
      .green_shift = layout->green_shift,
      .blue_shift = layout->blue_shift,
    };
    log_end(locked);
    return CALL_OK;
  }
  if (display->owner != process) {
    return CALL_DENIED;
  }
  if (operation == DISPLAY_REPLACE) {
    return replace_display(display, process, generation, reply_address, &reply->buffer);
  }
  if (operation == DISPLAY_SUBMIT) {
    return submit_display(display, slot, &reply->submit);
  }
  KASSERT(operation == DISPLAY_RELEASE);
  release_display(display);
  return CALL_OK;
}

void display_request_execute(struct display_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->loan && request->display);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  request->result = service_display(request->display, request->loan,
      request->operation, request->generation, request->slot, request->reply_address,
      &request->reply);
  request->loan = NULL;
  request->display = NULL;
}

static enum call_status request_display(struct display_object *display,
    uint64_t operation, uint64_t generation, uint64_t slot, uintptr_t reply_address,
    union display_reply *reply)
{
  struct display_request *request =
      (struct display_request *)bsp_request_prepare(BSP_SERVICE_DISPLAY);
  request->loan = process_current();
  KASSERT(request->loan);
  request->display = display;
  request->operation = operation;
  request->generation = generation;
  request->slot = slot;
  request->reply_address = reply_address;
  request->reply = (union display_reply){0};

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
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != DISPLAY_ACQUIRE && operation != DISPLAY_SUBMIT &&
      operation != DISPLAY_RELEASE && operation != DISPLAY_SIZE &&
      operation != DISPLAY_REPLACE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & DISPLAY_RIGHT_DRAW)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  /* REPLACE carries a generation and SUBMIT a slot, each one u64. */
  uint64_t argument = 0;
  if (operation == DISPLAY_REPLACE || operation == DISPLAY_SUBMIT) {
    if (request_size != sizeof(argument)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&argument, request_address, sizeof(argument))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  union display_reply reply = {0};
  size_t reply_size = operation == DISPLAY_ACQUIRE || operation == DISPLAY_REPLACE ?
    sizeof(reply.buffer) :
    operation == DISPLAY_SIZE ? sizeof(reply.size) :
    operation == DISPLAY_SUBMIT ? sizeof(reply.submit) : 0;
  if (reply_size) {
    if (reply_capacity < reply_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  }

  enum call_status status = request_display(display, operation,
      operation == DISPLAY_REPLACE ? argument : 0,
      operation == DISPLAY_SUBMIT ? argument : 0, reply_address, &reply);
  if (status != CALL_OK || !reply_size) {
    return (struct syscall_result){status, 0};
  }
  /* New mappings are disjoint; REPLACE refuses to retire the checked reply. */
  KASSERT(copy_to_user(reply_address, &reply, reply_size));
  return (struct syscall_result){CALL_OK, reply_size};
}
