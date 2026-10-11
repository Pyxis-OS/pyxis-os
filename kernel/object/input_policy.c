#include <kernel/input.h>
#include <kernel/mm/heap.h>
#include <kernel/object/input_policy.h>
#include <kernel/user_memory.h>

static void
destroy_input_policy(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *
input_policy_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_INPUT_POLICY, destroy_input_policy);
  }
  return object;
}

void
input_policy_execute(struct input_policy_install_request *request)
{
  request->status = input_policy_install(request->mouse_direction,
      request->touchpad_direction) ? CALL_OK : CALL_DENIED;
}

struct syscall_result
input_policy_call(uint64_t rights, uint64_t operation, uintptr_t address, size_t bytes)
{
  if (operation != INPUT_POLICY_INSTALL_ONCE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & INPUT_POLICY_RIGHT_INSTALL_ONCE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  /* The dispatcher has consumed the message header. */
  struct { uint32_t mouse, touchpad; } value;
  if (bytes != sizeof(value)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&value, address, sizeof(value))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (value.mouse > INPUT_SCROLL_TRADITIONAL || value.touchpad > INPUT_SCROLL_TRADITIONAL) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct input_policy_install_request *request = (void *)
      bsp_request_prepare(BSP_SERVICE_INPUT_POLICY);
  request->mouse_direction = value.mouse;
  request->touchpad_direction = value.touchpad;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}
