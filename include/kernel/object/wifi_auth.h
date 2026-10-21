#ifndef KERNEL_OBJECT_WIFI_AUTH_H
#define KERNEL_OBJECT_WIFI_AUTH_H

#include <abi/wifi.h>
#include <abi/syscall.h>
#include <kernel/object/capability.h>
#include <kernel/object/forwarder.h>
#include <kernel/service/request.h>

struct process;
struct wifi_auth;
struct wifi_connect_work;

struct wifi_auth_request {
  struct bsp_request request;
  struct wifi_auth *auth;
  struct kernel_object *radio;
  struct process *owner; /* Admission only; cleared before FORWARDED. */
  uint64_t operation, generation, deadline_ns;
  bool in_flight;
  enum call_status status;
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  union {
    struct wifi_auth_send_request send;
    struct wifi_auth_key_request key;
    struct wifi_auth_authorize_request authorize;
    struct wifi_auth_fail_request fail;
  } input;
  union {
    struct wifi_auth_open_reply opened;
    struct wifi_auth_status_reply status;
    struct wifi_auth_event event;
  } reply;
};

struct syscall_result wifi_auth_open_call(struct kernel_object *radio, uint64_t rights,
    size_t size, uintptr_t output, size_t capacity);
struct syscall_result wifi_auth_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t input, size_t size, uintptr_t output, size_t capacity);
void wifi_auth_execute(struct wifi_auth_request *request);
bool wifi_auth_grant_retain(struct kernel_object *object, uint64_t rights);
void wifi_auth_grant_release(struct kernel_object *object, uint64_t rights);
bool wifi_auth_delegatable(const struct kernel_object *object, uint64_t rights);
bool wifi_auth_owned(const struct kernel_object *object, const struct process *owner);
uint64_t wifi_auth_ready(struct kernel_object *object, uint64_t events);
void wifi_auth_process_exit(struct process *process);
void wifi_auth_revocation_notify(void);

/* BSP/IF=0. Selection returns one storage reference, never a logical RUN or
 * owning-link grant. Caller releases it only after retiring every association. */
enum call_status wifi_auth_select(const struct forwarder_operation *lineage, struct wifi_auth **auth);
void wifi_auth_release(struct wifi_auth *auth);
/* Copies BEGIN data and retains the exact original credential grant; success
 * never consumes the caller's grant. Backing/READ proof belongs to CONNECT. */
enum call_status wifi_auth_begin(struct wifi_auth *auth, struct wifi_connect_work *work,
    uint64_t generation, uint64_t deadline, const struct wifi_auth_begin *begin,
    const struct capability_grant *credential);
enum call_status wifi_auth_receive(struct wifi_auth *auth, uint64_t generation,
    const void *eapol, size_t bytes, uint32_t flags);
void wifi_auth_retire(struct wifi_auth *auth, uint64_t generation, enum call_status status);
enum call_status wifi_auth_stop_reason(struct wifi_auth *auth, uint64_t generation);
/* BSP radio worker, IF=1. Take detaches nothing; complete detaches and erases
 * before waking the parked caller. No firmware call occurs inside this object. */
struct wifi_auth_request *wifi_auth_worker_take(struct wifi_connect_work *work, uint64_t generation);
void wifi_auth_worker_complete(struct wifi_auth_request *request, enum call_status status);

#endif
