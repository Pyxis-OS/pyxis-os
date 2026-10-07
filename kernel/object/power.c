#include <abi/power.h>
#include <kernel/acpi.h>
#include <kernel/mm/heap.h>
#include <kernel/object/power.h>

static void destroy_power(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *power_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_POWER, destroy_power);
  }
  return object;
}

struct syscall_result power_call(uint64_t rights, uint64_t operation,
    size_t request_size, size_t reply_capacity)
{
  enum acpi_power_action action;
  uint64_t required;
  if (operation == POWER_OFF) {
    action = ACPI_POWER_OFF;
    required = POWER_RIGHT_OFF;
  } else if (operation == POWER_RESTART) {
    action = ACPI_POWER_RESTART;
    required = POWER_RIGHT_RESTART;
  } else {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size || reply_capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct acpi_power_request *request = (void *)bsp_request_prepare(BSP_SERVICE_POWER);
  request->action = action;
  request->status = CALL_OK;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}
