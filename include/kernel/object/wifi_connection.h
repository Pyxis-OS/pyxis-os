#ifndef KERNEL_OBJECT_WIFI_CONNECTION_H
#define KERNEL_OBJECT_WIFI_CONNECTION_H

#include <kernel/object/wifi.h>

struct wifi_connection_request {
  struct bsp_request request;
  struct kernel_object *radio;
  struct kernel_object *attempt;
  struct process *owner;
  struct wifi_connect_request start;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  handle_t result;
  enum call_status status;
};

void wifi_connection_execute(struct wifi_connection_request *request);
void wifi_connection_process_exit(struct process *process);
struct syscall_result wifi_connection_start_call(struct kernel_object *radio, uint64_t rights,
    uintptr_t input, size_t size, uintptr_t output, size_t capacity);
struct syscall_result wifi_connection_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t size, uintptr_t output, size_t capacity);
bool wifi_connection_owned(const struct kernel_object *object, const struct process *owner);
bool wifi_connection_grant_retain(struct kernel_object *object, uint64_t rights);
void wifi_connection_grant_release(struct kernel_object *object, uint64_t rights);
uint64_t wifi_connection_ready(struct kernel_object *object, uint64_t events);

#endif
