#ifndef KERNEL_OBJECT_SYSTEM_INFO_H
#define KERNEL_OBJECT_SYSTEM_INFO_H

#include <abi/syscall.h>
#include <abi/system_info.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct system_info_memory_request {
  struct bsp_request request;
  struct system_info_memory reply;
};

struct system_info_hostname_request {
  struct bsp_request request;
  struct system_info_hostname value;
  enum call_status status;
};

/* SYSTEM_INFO_POWER, or SYSTEM_INFO_BATTERY with INDEX; found is false for an
 * index past the battery count. */
struct system_info_power_request {
  struct bsp_request request;
  uint64_t operation;
  uint64_t index;
  bool found;
  union {
    struct system_info_power power;
    struct system_info_battery battery;
  } reply;
};

/* BSP, IF=0, after all CPUs acknowledge boot and before scheduler publication.
 * No allocation; identity and CPU observations are immutable afterward. */
void system_info_init(void);
/* BSP, IF=0. Returns one owned reference to system-information authority. */
struct kernel_object *system_info_create(void);
struct syscall_result system_info_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity);
/* Executor only, BSP, IF=0. Never touches caller mappings or remote counters. */
void system_info_memory_execute(struct system_info_memory_request *request);
/* Executor only, BSP, IF=0. Copies the ACPI worker's latest published poll. */
void system_info_power_execute(struct system_info_power_request *request);
/* Executor only, BSP, IF=0. Publishes the immutable boot-wide hostname once. */
void system_info_hostname_execute(struct system_info_hostname_request *request);

#endif
