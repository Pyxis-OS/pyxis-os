#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/screen_capture.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_screen_capture(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *screen_capture_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_SCREEN_CAPTURE, destroy_screen_capture);
  }
  return object;
}

struct syscall_result screen_capture_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != SCREEN_CAPTURE_FRAME) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & SCREEN_CAPTURE_RIGHT_CAPTURE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size || reply_capacity < sizeof(struct screen_capture_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct screen_capture_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct process *process = process_current();
  KASSERT(process);
  struct screen_capture_request *request =
      (void *)bsp_request_prepare(BSP_SERVICE_SCREEN_CAPTURE);
  request->table = &process->capabilities;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct screen_capture_reply reply = request->reply;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
