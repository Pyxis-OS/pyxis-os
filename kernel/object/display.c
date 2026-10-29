#include <kernel/mm/kernel_owner.h>
#include <kernel/object/clipboard.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/display.h>
#include <kernel/pointer.h>
#include <kernel/display.h>
#include <kernel/log.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/space.h>
#include <kernel/task.h>
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

/* Metadata and references stay on the fixed display owner. */
static bool geometry_busy;
static size_t committing_transactions;

bool display_geometry_begin(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled() && !geometry_busy);
  if (committing_transactions) {
    return false;
  }
  geometry_busy = true;
  return true;
}

void display_geometry_end(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled() && geometry_busy);
  geometry_busy = false;
}

static void release_frame(struct display_frame *frame, struct display_retirement *retirement)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled() && frame->references);
  if (!--frame->references) {
    display_frame_retire(frame, retirement);
  }
}

void display_frame_release(struct display_frame *frame)
{
  release_frame(frame, NULL);
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
    if ((offset / PAGE_SIZE + 1) % 64 == 0) {
      task_yield_masked();
    }
  }
}

static size_t slot_size(const struct framebuffer *layout)
{
  return (layout->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static enum call_status allocate_frame(const struct framebuffer *layout,
    struct display_frame **result)
{
  struct display_frame *frame = kmalloc(sizeof(*frame));
  if (!frame) {
    return CALL_NO_MEMORY;
  }
  *frame = (struct display_frame){.fb = *layout, .references = 1,
      .preparation_cpu = arch_cpu_index()};
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    cpu_enable_interrupts();
    bool admitted = vm_kernel_owner_acquire(&owner);
    cpu_disable_interrupts();
    if (!admitted) {
      kfree(frame);
      return CALL_BUSY;
    }
  }
  size_t bytes = slot_size(layout);
  enum mm_result status = vm_reserve(vm_kernel_space(), bytes, PAGE_SIZE, &frame->fb.address);
  if (status == MM_OK) {
    struct vm_backing backing = {0};
    status = vm_back_begin(vm_kernel_space(), frame->fb.address, bytes, PAGE_WRITE, &backing);
    if (status == MM_OK) {
      while (!vm_back_step(vm_kernel_space(), &backing, 64)) {
        task_yield_masked();
      }
      status = backing.status;
    }
    if (status != MM_OK) {
      KASSERT(vm_release(vm_kernel_space(), frame->fb.address, bytes) == MM_OK);
    }
  }
  vm_kernel_owner_release(&owner);
  if (status != MM_OK) {
    kfree(frame);
    return status == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
  }
  *result = frame;
  return CALL_OK;
}

/* Unpublished backing has only been accessed by its preparation actor. */
static void discard_frame(struct display_frame *frame)
{
  KASSERT(frame->preparation_cpu == arch_cpu_index() && frame->references == 1);
  struct vm_kernel_owner owner = {0};
  cpu_enable_interrupts();
  vm_kernel_owner_cleanup(&owner);
  cpu_disable_interrupts();
  struct vm_reclaim reclaim = {0};
  KASSERT(vm_free_begin(vm_kernel_space(), frame->fb.address, slot_size(&frame->fb),
      &reclaim) == MM_OK);
  while (!vm_free_step(&reclaim, 64)) {
    task_yield_masked();
  }
  vm_kernel_owner_release(&owner);
  kfree(frame);
}

static enum call_status map_frame(struct process *process, const struct display_frame *frame,
    uintptr_t address)
{
  size_t size = slot_size(&frame->fb);
  for (size_t mapped = 0; mapped < size; mapped += PAGE_SIZE) {
    struct page_translation page;
    KASSERT(vm_query(vm_kernel_space(), frame->fb.address + mapped, &page) == MM_OK);
    enum mm_result result = vm_map(process->address_space, address + mapped,
        page.physical, PAGE_USER | PAGE_WRITE);
    if (result != MM_OK) {
      unmap_pixels(process, address, mapped);
      return result == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
    }
    if ((mapped / PAGE_SIZE + 1) % 64 == 0) {
      task_yield_masked();
    }
  }
  return CALL_OK;
}

/* Allocates and maps one zeroed slot set at a disjoint user range. Failure
 * leaves nothing allocated or mapped. */
static enum call_status prepare_slots(struct display_request *request)
{
  struct process *process = request->loan;
  struct display_frame **slots = request->slots;
  struct display_buffer *reply = &request->reply.buffer;
  const struct framebuffer *layout = &request->layout;
  if (!layout->size || layout->size > SIZE_MAX - (PAGE_SIZE - 1) ||
      slot_size(layout) > SIZE_MAX / DISPLAY_SLOT_COUNT) {
    return CALL_LIMIT;
  }
  size_t size = slot_size(layout);
  size_t prepared = 0;
  enum call_status status = CALL_OK;
  for (; prepared < DISPLAY_SLOT_COUNT; ++prepared) {
    status = allocate_frame(layout, &slots[prepared]);
    if (status != CALL_OK) {
      break;
    }
  }
  uintptr_t address = 0;
  if (status == CALL_OK) {
    enum mm_result result = vm_reserve(process->address_space, size * DISPLAY_SLOT_COUNT,
        PAGE_SIZE, &address);
    if (result != MM_OK) {
      status = result == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
    }
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
      discard_frame(slots[--prepared]);
    }
    return status;
  }

  *reply = (struct display_buffer){
    .address = address,
    .size = size,
    .width = layout->width,
    .height = layout->height,
    .pitch = layout->pitch,
    .generation = request->geometry,
    .red_shift = layout->red_shift,
    .green_shift = layout->green_shift,
    .blue_shift = layout->blue_shift,
    .slot_count = DISPLAY_SLOT_COUNT,
  };
  return CALL_OK;
}

/* A preempted presenter, or the session's on-screen reference, may still hold
 * a retired frame. The last holder releases the backing and pending cleanup. */
static void retire_frame(struct process *process, struct display_frame *frame,
    struct display_retirement *retirement)
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
  release_frame(frame, retirement);
}

/* Private aliases are gone. The presenter may retain its own current frame. */
static void retire_slots(struct display_object *display, struct process *process,
    struct display_retirement *retirement)
{
  for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
    retire_frame(process, display->slots[i], retirement);
    display->slots[i] = NULL;
  }
  display->pending = DISPLAY_SLOT_COUNT;
}

static void release_display(struct display_object *display,
    struct display_retirement *retirement)
{
  clipboard_space_cancel(display->space);
  pointer_surface_ended(display->space);
  struct display_frame *current = display->current;
  display->current = NULL;
  retire_slots(display, display->owner, retirement);
  if (current) {
    retire_frame(display->owner, current, retirement);
  }
  display->presented = false;
  display->visible = false;
  display->owner = NULL;
  display->user_address = 0;
  ++display->visual_generation;
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
  ++display->visual_generation;
  space_display_changed(display->space, graphics);
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
  ++display->visual_generation;
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
    struct process *process, uint64_t operation, uint64_t slot,
    union display_reply *reply)
{
  KASSERT(arch_cpu_index() == 0);
  if (display->space != process->space) {
    return CALL_DENIED;
  }
  if (operation != DISPLAY_RELEASE && !display_available()) {
    return CALL_UNAVAILABLE;
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
  KASSERT(operation == DISPLAY_SUBMIT);
  return submit_display(display, slot, &reply->submit);
}

static enum call_status reserve_display(struct display_request *request)
{
  struct display_object *display = request->display;
  struct process *process = request->loan;
  if (display->space != process->space) {
    return CALL_DENIED;
  }
  if (display->transaction || display->retiring_process) {
    return CALL_BUSY;
  }
  if (request->operation != DISPLAY_RELEASE && (!display_available() || geometry_busy)) {
    return geometry_busy ? CALL_BUSY : CALL_UNAVAILABLE;
  }
  if (request->operation == DISPLAY_ACQUIRE) {
    if (display->owner) {
      return CALL_BUSY;
    }
  } else if (display->owner != process) {
    return CALL_DENIED;
  }
  if (request->operation != DISPLAY_RELEASE && display->mapping_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  request->geometry = display->space->tty->geometry_generation;
  if (request->operation == DISPLAY_REPLACE && request->generation != request->geometry) {
    return CALL_BUSY;
  }
  request->layout = *display->space->fb;
  request->mapping_identity = display->mapping_identity;
  if (display->owner) {
    request->previous_address = display->user_address;
    request->previous_size = slot_size(&display->slots[0]->fb) * DISPLAY_SLOT_COUNT;
    if (request->operation == DISPLAY_REPLACE &&
        request->reply_address < request->previous_address + request->previous_size &&
        request->previous_address < request->reply_address + sizeof(struct display_buffer)) {
      return CALL_BAD_BUFFER;
    }
  }
  display->transaction = request;
  return CALL_OK;
}

static enum call_status begin_commit(struct display_request *request)
{
  struct display_object *display = request->display;
  KASSERT(display->transaction == request && !request->committing);
  if (request->operation != DISPLAY_RELEASE &&
      (geometry_busy || request->geometry != display->space->tty->geometry_generation)) {
    return CALL_BUSY;
  }
  KASSERT(display->mapping_identity == request->mapping_identity);
  if (!task_operation_start()) {
    return CALL_DENIED;
  }
  ++committing_transactions;
  request->committing = true;
  return CALL_OK;
}

static void end_control(struct display_request *request)
{
  KASSERT(request->display->transaction == request);
  request->display->transaction = NULL;
  if (request->committing) {
    KASSERT(committing_transactions);
    --committing_transactions;
    request->committing = false;
  }
}

static void publish_display(struct display_request *request)
{
  struct display_object *display = request->display;
  KASSERT(display->transaction == request && request->committing);
  if (request->operation == DISPLAY_RELEASE) {
    release_display(display, &request->retirement);
  } else {
    if (request->operation == DISPLAY_REPLACE) {
      retire_slots(display, request->loan, &request->retirement);
    }
    for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
      display->slots[i] = request->slots[i];
      request->slots[i] = NULL;
    }
    if (request->operation == DISPLAY_ACQUIRE) {
      display->pending = DISPLAY_SLOT_COUNT;
      display->owner = request->loan;
    }
    display->user_address = request->reply.buffer.address;
    clipboard_space_cancel(display->space);
    ++display->mapping_identity;
    if (request->operation == DISPLAY_REPLACE) {
      pointer_geometry_changed(display->space);
    }
    ++display->visual_generation;
  }
  end_control(request);
}

void display_request_execute(struct display_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (request->control == DISPLAY_CONTROL_DIRECT) {
    if (!task_operation_start()) {
      request->result = CALL_DENIED;
      return;
    }
    request->result = service_display(request->display, request->loan,
        request->operation, request->slot, &request->reply);
    return;
  }
  struct display_request *transaction = request;
  if (request->control == DISPLAY_CONTROL_RESERVE) {
    request->control_result = reserve_display(transaction);
  } else if (request->control == DISPLAY_CONTROL_COMMIT) {
    request->control_result = begin_commit(transaction);
  } else if (request->control == DISPLAY_CONTROL_PUBLISH) {
    publish_display(transaction);
    request->control_result = CALL_OK;
  } else {
    KASSERT(request->control == DISPLAY_CONTROL_CANCEL);
    end_control(transaction);
    request->control_result = CALL_OK;
  }
}

static enum call_status control_display(struct display_request *transaction,
    enum display_control control)
{
  KASSERT(!vm_kernel_owner_held());
  KASSERT(&transaction->request == task_bsp_request_current());
  KASSERT(transaction->request.state == BSP_REQUEST_PREPARED ||
      transaction->request.state == BSP_REQUEST_COMPLETE);
  transaction->request = (struct bsp_request){
    .service = BSP_SERVICE_DISPLAY, .state = BSP_REQUEST_PREPARED,
    .wait = task_wait_prepare(), .cleanup_group = transaction->vm_request.cleanup_group,
    .operation = transaction->vm_request.operation,
  };
  transaction->control = control;
  bsp_request_submit_and_wait(&transaction->request);
  return transaction->control_result;
}

static void unmap_slots(struct process *process, uintptr_t address, size_t bytes)
{
  if (address) {
    unmap_pixels(process, address, bytes);
    KASSERT(vm_release(process->address_space, address, bytes) == MM_OK);
  }
}

void display_vm_request_execute(struct bsp_request *base)
{
  struct display_request *request = (void *)((char *)base -
      offsetof(struct display_request, vm_request));
  struct process *process = request->loan;
  process_vm_require_mutation(process);
  display_retirement_init(&request->retirement);
  if (request->operation != DISPLAY_RELEASE &&
      !process_vm_buffer_check(process, request->reply_address, sizeof(struct display_buffer), true)) {
    request->result = CALL_BAD_BUFFER;
    return;
  }
  request->result = control_display(request, DISPLAY_CONTROL_RESERVE);
  if (request->result != CALL_OK) {
    return;
  }
  if (request->operation != DISPLAY_RELEASE) {
    request->result = prepare_slots(request);
  }
  if (request->result == CALL_OK) {
    request->result = task_stop_requested() ? CALL_DENIED :
        control_display(request, DISPLAY_CONTROL_COMMIT);
  }
  if (request->result != CALL_OK) {
    if (request->reply.buffer.address) {
      unmap_slots(process, request->reply.buffer.address,
          request->reply.buffer.size * DISPLAY_SLOT_COUNT);
      for (size_t i = 0; i < DISPLAY_SLOT_COUNT; ++i) {
        discard_frame(request->slots[i]);
        request->slots[i] = NULL;
      }
    }
    KASSERT(control_display(request, DISPLAY_CONTROL_CANCEL) == CALL_OK);
    return;
  }
  unmap_slots(process, request->previous_address, request->previous_size);
  KASSERT(control_display(request, DISPLAY_CONTROL_PUBLISH) == CALL_OK);
  display_retirement_wait(&request->retirement);
  request->result = CALL_OK;
}

static enum call_status request_display(struct display_object *display,
    uint64_t operation, uint64_t generation, uint64_t slot, uintptr_t reply_address,
    union display_reply *reply, bool *reply_owned)
{
  struct process *process = process_current();
  if (operation == DISPLAY_ACQUIRE || operation == DISPLAY_REPLACE || operation == DISPLAY_RELEASE) {
    /* Both typed records live in provisioned shared task storage. The VM
     * header stays local; only its control header enters the BSP queue. */
    struct display_request *transaction = (void *)bsp_request_prepare(BSP_SERVICE_DISPLAY);
    transaction->vm_request = (struct bsp_request){
      .service = BSP_SERVICE_DISPLAY, .state = BSP_REQUEST_PREPARED,
      .vm_process = process, .vm_mutator = task_current_user(),
      .cleanup_group = transaction->request.cleanup_group,
      .operation = transaction->request.operation,
    };
    transaction->loan = process;
    transaction->display = display;
    transaction->operation = operation;
    transaction->generation = generation;
    transaction->reply_address = reply_address;
    process_vm_private_call(&transaction->vm_request);
    *reply = transaction->reply;
    *reply_owned = transaction->vm_request.vm_reply_owned;
    enum call_status result = transaction->result;
    if (transaction->request.state == BSP_REQUEST_PREPARED) {
      bsp_request_cancel_prepared(&transaction->request);
    } else {
      bsp_request_release(&transaction->request);
    }
    return result;
  }
  struct display_request *request = (void *)bsp_request_prepare(BSP_SERVICE_DISPLAY);
  request->loan = process;
  request->display = display;
  request->operation = operation;
  request->slot = slot;
  bsp_request_submit_and_wait(&request->request);
  *reply = request->reply;
  *reply_owned = false;
  enum call_status result = request->result;
  bsp_request_release(&request->request);
  return result;
}

void display_process_exit_prepare(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  struct display_object *display = process->space->display;
  display_retirement_init(&process->display_exit.retirement);
  if (display->owner == process) {
    KASSERT(!display->transaction && !display->retiring_process);
    display->retiring_process = process;
    process->display_exit.display = display;
    process->display_exit.address = display->user_address;
    process->display_exit.bytes = slot_size(&display->slots[0]->fb) * DISPLAY_SLOT_COUNT;
  }
}

void display_process_exit_unmap(struct process *process)
{
  KASSERT(arch_cpu_index() == process->retirement_cpu && !cpu_interrupts_enabled());
  unmap_slots(process, process->display_exit.address, process->display_exit.bytes);
}

void display_process_exit_finish(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  struct display_object *display = process->display_exit.display;
  if (display) {
    KASSERT(display->retiring_process == process && display->owner == process);
    release_display(display, &process->display_exit.retirement);
    display->retiring_process = NULL;
    process->display_exit.display = NULL;
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

  bool reply_owned;
  enum call_status status = request_display(display, operation,
      operation == DISPLAY_REPLACE ? argument : 0,
      operation == DISPLAY_SUBMIT ? argument : 0, reply_address, &reply, &reply_owned);
  size_t copied = 0;
  if (status == CALL_OK && reply_size) {
    bool valid = copy_to_user(reply_address, &reply, reply_size);
    /* Mapping mutations rechecked output under the exclusive transaction. */
    KASSERT(valid || !reply_owned);
    if (valid) {
      copied = reply_size;
    } else {
      status = CALL_BAD_BUFFER;
    }
  }
  if (reply_owned) {
    process_vm_reply_finish(process_current());
  }
  return (struct syscall_result){status, copied};
}
