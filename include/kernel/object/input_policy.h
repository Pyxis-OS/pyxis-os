#ifndef KERNEL_OBJECT_INPUT_POLICY_H
#define KERNEL_OBJECT_INPUT_POLICY_H

#include <abi/input_policy.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct input_policy_install_request {
  struct bsp_request request;
  uint32_t mouse_direction, touchpad_direction;
  int32_t touchpad_speed;
  enum call_status status;
};

struct kernel_object *input_policy_create(void);
struct syscall_result input_policy_call(uint64_t rights, uint64_t operation,
    uintptr_t address, size_t bytes);
void input_policy_execute(struct input_policy_install_request *request);

#endif
