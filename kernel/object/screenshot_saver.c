#include <abi/screen_capture.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/capability.h>
#include <kernel/object/screen_capture.h>
#include <kernel/object/screenshot_saver.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/string.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user_memory.h>

struct screenshot_saver {
  struct kernel_object object;
  struct space *space;
  atomic_size_t grants;
  uint64_t next_id, id, selection, feedback;
  bool delivered, attempted, captured;
};

static void destroy_saver(struct kernel_object *object)
{
  struct screenshot_saver *saver = (void *)object;
  KASSERT(saver->space->screenshot_saver == object);
  saver->space->screenshot_saver = NULL;
  kfree(saver);
}

struct kernel_object *screenshot_saver_create(struct space *space)
{
  KASSERT(arch_cpu_index() == 0 && !space->screenshot_saver);
  struct screenshot_saver *saver = kmalloc(sizeof(*saver));
  if (!saver) {
    return NULL;
  }
  memset(saver, 0, sizeof(*saver));
  object_init(&saver->object, OBJECT_SCREENSHOT_SAVER, destroy_saver);
  saver->space = space;
  space->screenshot_saver = &saver->object;
  return &saver->object;
}

bool screenshot_saver_grant_retain(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & SCREENSHOT_SAVER_RIGHT_SERVE)) {
    return true;
  }
  struct screenshot_saver *saver = (void *)object;
  size_t count = atomic_load_explicit(&saver->grants, memory_order_relaxed);
  do {
    if (count == SIZE_MAX) {
      return false;
    }
  } while (!atomic_compare_exchange_weak_explicit(&saver->grants, &count, count + 1,
      memory_order_release, memory_order_relaxed));
  return true;
}

void screenshot_saver_grant_release(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & SCREENSHOT_SAVER_RIGHT_SERVE)) {
    return;
  }
  struct screenshot_saver *saver = (void *)object;
  size_t before = atomic_fetch_sub_explicit(&saver->grants, 1, memory_order_acq_rel);
  KASSERT(before);
  if (before == 1) {
    readiness_notify();
  }
}

bool screenshot_saver_in_space(const struct kernel_object *object, const struct space *space)
{
  return ((const struct screenshot_saver *)object)->space == space;
}

static bool live(const struct screenshot_saver *saver)
{
  return atomic_load_explicit(&saver->grants, memory_order_acquire) != 0;
}

uint64_t screenshot_saver_ready(struct kernel_object *object)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0);
  struct screenshot_saver *saver = (void *)object;
  uint64_t events = !live(saver) ? WAIT_ERROR :
      saver->id && !saver->delivered ? WAIT_READABLE : 0;
  cpu_restore_interrupts(flags);
  return events;
}

void screenshot_saver_key(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  struct screenshot_saver *saver = (void *)space->screenshot_saver;
  if (!saver || !live(saver)) {
    space_screenshot_feedback(CALL_UNAVAILABLE, NULL);
    return;
  }
  if (saver->id) {
    space_screenshot_feedback(CALL_BUSY, NULL);
    return;
  }
  if (saver->next_id == UINT64_MAX) {
    space_screenshot_feedback(CALL_LIMIT, NULL);
    return;
  }
  saver->id = ++saver->next_id;
  saver->selection = space_selection_generation();
  saver->feedback = space_screenshot_feedback_begin();
  saver->delivered = saver->attempted = saver->captured = false;
  readiness_notify();
}

bool screenshot_saver_capture_begin(struct screenshot_saver *saver, uint64_t id)
{
  KASSERT(arch_cpu_index() == 0);
  if (!live(saver) || saver->id != id || !saver->delivered || saver->attempted) {
    return false;
  }
  saver->attempted = true;
  return space_pointer_active() == saver->space &&
      space_selection_generation() == saver->selection;
}

void screenshot_saver_capture_finished(struct screenshot_saver *saver, uint64_t id, bool success)
{
  KASSERT(arch_cpu_index() == 0);
  if (success && saver->id == id) {
    saver->captured = true;
  }
}

void screenshot_saver_execute(struct screenshot_saver_request *request)
{
  struct screenshot_saver *saver = request->saver;
  request->status = CALL_DENIED;
  if (!task_operation_start() || !live(saver)) {
    return;
  }
  if (request->operation == SCREENSHOT_SAVER_NEXT) {
    if (!saver->id || saver->delivered) {
      request->status = CALL_WOULD_BLOCK;
      return;
    }
    request->pending = (struct screenshot_saver_pending){.request = saver->id};
    memcpy(request->pending.space, saver->space->name, sizeof(request->pending.space));
    saver->delivered = true;
  } else {
    if (!request->id || saver->id != request->id || !saver->delivered ||
        (request->completion == CALL_OK && !saver->captured)) {
      return;
    }
    space_screenshot_complete(saver->feedback, request->completion, request->filename);
    saver->id = 0;
    saver->delivered = saver->attempted = saver->captured = false;
  }
  request->status = CALL_OK;
}

static struct syscall_result capture(struct screenshot_saver *saver,
    uintptr_t address, size_t bytes, uintptr_t reply_address, size_t reply_capacity)
{
  struct { uint64_t request, capture; } value;
  if (bytes != sizeof(value)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&value, address, sizeof(value))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reference reference;
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      value.capture, 0, 0, &reference);
  if (result != CAP_OK) {
    return (struct syscall_result){result == CAP_BAD_HANDLE ? CALL_BAD_HANDLE :
        result == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED, 0};
  }
  result = capability_reference_resolve(&reference);
  struct syscall_result reply = {CALL_DENIED, 0};
  if (result == CAP_OK && reference.object->type == OBJECT_SCREEN_CAPTURE) {
    reply = screen_capture_saver_call(reference.rights, saver, value.request,
        reply_address, reply_capacity);
  } else if (result == CAP_OK) {
    reply.status = CALL_WRONG_TYPE;
  }
  capability_release(&reference);
  return reply;
}

struct syscall_result screenshot_saver_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t address, size_t bytes,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & SCREENSHOT_SAVER_RIGHT_SERVE) ||
      !screenshot_saver_in_space(object, process_current()->space)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == SCREENSHOT_SAVER_CAPTURE) {
    return capture((void *)object, address, bytes, reply_address, reply_capacity);
  }
  struct { uint64_t request, status, filename, length; } value = {0};
  char filename[SCREENSHOT_SAVER_FILENAME_MAX + 1] = {0};
  if (operation == SCREENSHOT_SAVER_NEXT) {
    if (bytes || reply_capacity < sizeof(struct screenshot_saver_pending)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(struct screenshot_saver_pending), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else if (operation == SCREENSHOT_SAVER_COMPLETE) {
    if (bytes != sizeof(value)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&value, address, sizeof(value))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (value.status >= CALL_STATUS_COUNT || value.length > SCREENSHOT_SAVER_FILENAME_MAX ||
        (value.status == CALL_OK ? !value.length : value.length != 0)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (value.length && !copy_from_user(filename, value.filename, value.length)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    for (size_t i = 0; i < value.length; ++i) {
      if ((unsigned char)filename[i] < 0x20 || (unsigned char)filename[i] > 0x7e ||
          filename[i] == '/' || filename[i] == '\\') {
        return (struct syscall_result){CALL_BAD_REQUEST, 0};
      }
    }
  } else {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct screenshot_saver_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_SCREENSHOT_SAVER);
  request->pending = (struct screenshot_saver_pending){0};
  request->saver = (void *)object;
  request->operation = operation;
  request->id = value.request;
  request->completion = value.status;
  memcpy(request->filename, filename, sizeof(filename));
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct screenshot_saver_pending pending = request->pending;
  bsp_request_release(&request->request);
  size_t size = 0;
  if (status == CALL_OK && operation == SCREENSHOT_SAVER_NEXT) {
    if (!copy_to_user(reply_address, &pending, sizeof(pending))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    size = sizeof(pending);
  }
  return (struct syscall_result){status, size};
}
