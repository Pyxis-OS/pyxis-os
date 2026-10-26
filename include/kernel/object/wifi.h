#ifndef KERNEL_OBJECT_WIFI_H
#define KERNEL_OBJECT_WIFI_H

#include <abi/wifi.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct process;
struct wifi_object;
struct wifi_scan;

struct wifi_request {
  struct bsp_request request;
  struct wifi_object *radio; /* Borrowed only through synchronous admission. */
  struct wifi_scan *scan;
  struct process *owner;
  uint64_t operation;
  struct wifi_scan_request start;
  struct wifi_policy_request policy;
  struct wifi_credential credential;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  union {
    struct wifi_status_full_reply status;
    struct wifi_scan_start_reply started;
    struct wifi_scan_take_reply taken;
    struct wifi_credential_reply credential;
  } reply;
  enum call_status status;
};

/* BSP/IF=0. Retains only the independent control identity, never the process. */
struct kernel_object *wifi_create(struct process *policy_owner);
bool wifi_policy_owned(const struct kernel_object *object, const struct process *process);
bool wifi_grant_delegatable(const struct kernel_object *object, uint64_t rights);
/* BSP/IF=0, before process VM teardown; workers retain no process pointer. */
void wifi_process_exit(struct process *process);
void wifi_request_execute(struct wifi_request *request);
struct syscall_result wifi_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t address, size_t size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result wifi_scan_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t size, uintptr_t reply_address, size_t reply_capacity);
bool wifi_scan_owned(const struct kernel_object *object, const struct process *process);
bool wifi_scan_grant_retain(struct kernel_object *object);
void wifi_scan_grant_release(struct kernel_object *object);
uint64_t wifi_scan_ready(struct kernel_object *object, uint64_t events);

#endif
