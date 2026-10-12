#include <abi/directory.h>
#include <abi/file.h>
#include <abi/tcp.h>
#include <abi/udp.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/completion.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

/* One unacknowledged native completion per process; second SUBMIT is CALL_BUSY.
 * Multiple receivers do not bypass it. See docs/technical-debt.md#native-completion-per-process-admission. */
#define NATIVE_COMPLETION_PER_PROCESS_LIMIT 1u
#define COMPLETION_STARTED (UINT64_C(1) << 32)
#define COMPLETION_REASON_MASK UINT64_C(0xffffffff)

struct completion_gate {
  struct kernel_object storage;
  atomic_uint pending;
};

static void destroy_gate(struct kernel_object *object)
{
  struct completion_gate *gate = (struct completion_gate *)object;
  KASSERT(!atomic_load_explicit(&gate->pending, memory_order_acquire));
  kfree(gate);
}

static struct completion_gate *process_gate(struct process *process)
{
  struct completion_gate *gate = atomic_load_explicit(&process->completion_gate, memory_order_acquire);
  if (!gate) {
    gate = kmalloc(sizeof(*gate));
    if (!gate) {
      return NULL;
    }
    *gate = (struct completion_gate){0};
    object_init(&gate->storage, OBJECT_COMPLETION_STORAGE, destroy_gate);
    atomic_init(&gate->pending, 0);
    atomic_store_explicit(&process->completion_gate, gate, memory_order_release);
  }
  return gate;
}

static bool process_busy(struct process *process)
{
  struct completion_gate *gate = atomic_load_explicit(&process->completion_gate, memory_order_acquire);
  return gate && atomic_load_explicit(&gate->pending, memory_order_acquire);
}

void native_completion_process_exit(struct process *process)
{
  struct completion_gate *gate = atomic_exchange_explicit(&process->completion_gate, NULL,
      memory_order_acq_rel);
  if (gate) {
    object_release(&gate->storage);
  }
}

static void destroy_operation(struct kernel_object *object)
{
  struct native_completion *operation = (struct native_completion *)object;
  KASSERT(!operation->delivery && !operation->backend);
  struct execution_group *group = operation->cleanup_group;
  struct execution_group *previous = object_cleanup_enter(group);
  capability_release(&operation->target);
  capability_grant_release(&operation->returned);
  object_release(&operation->gate->storage);
  memset(operation->input, 0, sizeof(operation->input));
  memset(operation->output, 0, sizeof(operation->output));
  kfree(operation);
  object_cleanup_leave(previous);
  if (group) {
    execution_group_cleanup_end(group);
  }
}

void native_completion_ack(struct native_completion *operation)
{
  KASSERT(operation->delivery && !operation->backend);
  operation->delivery = NULL;
  unsigned previous = atomic_exchange_explicit(&operation->gate->pending, 0, memory_order_acq_rel);
  KASSERT(previous == NATIVE_COMPLETION_PER_PROCESS_LIMIT);
  object_release(&operation->storage);
}

enum call_status native_completion_cancel_reason(struct native_completion *operation)
{
  return atomic_load_explicit(&operation->control, memory_order_acquire) & COMPLETION_REASON_MASK;
}

void native_completion_request_cancel(struct native_completion *operation, enum call_status reason)
{
  KASSERT(reason == CALL_ABANDONED || reason == CALL_TIMED_OUT || reason == CALL_ENDPOINT_CLOSED);
  uint64_t control = atomic_load_explicit(&operation->control, memory_order_acquire);
  while (!(control & COMPLETION_REASON_MASK)) {
    if (atomic_compare_exchange_weak_explicit(&operation->control, &control,
        control | reason, memory_order_acq_rel, memory_order_acquire)) {
      break;
    }
  }
  if (operation->protocol == PROTOCOL_TCP_SERVICE || operation->protocol == PROTOCOL_UDP) {
    native_completion_net_cancel(operation);
  } else {
    native_completion_fs_cancel(operation);
  }
}

bool native_completion_mark_started(struct native_completion *operation)
{
  uint64_t flags = cpu_save_interrupts();
  if (task_deadline_expired(operation->deadline_ns)) {
    native_completion_request_cancel(operation, CALL_TIMED_OUT);
  }
  uint64_t control = atomic_load_explicit(&operation->control, memory_order_acquire);
  for (;;) {
    if (control & COMPLETION_REASON_MASK) {
      cpu_restore_interrupts(flags);
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&operation->control, &control,
        control | COMPLETION_STARTED, memory_order_acq_rel, memory_order_acquire)) {
      cpu_restore_interrupts(flags);
      return true;
    }
  }
}

uint64_t native_completion_flags(struct native_completion *operation)
{
  uint64_t control = atomic_load_explicit(&operation->control, memory_order_acquire);
  return (!(control & COMPLETION_STARTED) ? COMPLETION_FLAG_NOT_STARTED : 0) |
      (control & COMPLETION_REASON_MASK ? COMPLETION_FLAG_CANCEL_REQUESTED : 0) |
      (operation->progress_known ? COMPLETION_FLAG_PROGRESS_KNOWN : 0);
}

void native_completion_finish(struct native_completion *operation, enum call_status status,
    const void *output, size_t size, struct capability_grant *grant)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(operation->delivery && !operation->backend && size <= sizeof(operation->output));
  KASSERT(!size || output);
  operation->result = status;
  operation->output_size = size;
  if (size && output != operation->output) {
    memcpy(operation->output, output, size);
  }
  if (grant) {
    operation->returned = *grant;
    *grant = (struct capability_grant){0};
  }
  /* Cancellation metadata never rewrites the native result or its known data. */
  if (task_deadline_expired(operation->deadline_ns)) {
    native_completion_request_cancel(operation, CALL_TIMED_OUT);
  }
  struct execution_group *previous = object_cleanup_enter(operation->cleanup_group);
  capability_release(&operation->target);
  object_cleanup_leave(previous);
  endpoint_completion_publish(operation);
}

static enum call_status reserve_backend(struct native_completion *operation)
{
  if (operation->protocol == PROTOCOL_TCP_SERVICE || operation->protocol == PROTOCOL_UDP) {
    return native_completion_net_reserve(operation);
  }
  return native_completion_fs_reserve(operation);
}

static void discard_backend(struct native_completion *operation)
{
  if (operation->protocol == PROTOCOL_TCP_SERVICE || operation->protocol == PROTOCOL_UDP) {
    native_completion_net_discard(operation);
  } else {
    native_completion_fs_discard(operation);
  }
}

static void start_backend(struct native_completion *operation)
{
  if (operation->protocol == PROTOCOL_TCP_SERVICE || operation->protocol == PROTOCOL_UDP) {
    native_completion_net_start(operation);
  } else {
    native_completion_fs_start(operation);
  }
}

static enum call_status deadline_status(uint64_t deadline, uint64_t maximum)
{
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  return deadline - now > maximum ? CALL_BAD_REQUEST : CALL_OK;
}

void completion_admit_execute(struct completion_admit_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct completion_gate *gate = process_gate(request->owner);
  uint64_t maximum = request->protocol == PROTOCOL_UDP && request->operation == UDP_SEND ?
      UDP_SEND_MAX_WAIT_NS : UINT64_C(30000000000);
  enum call_status status = gate ? deadline_status(request->deadline_ns, maximum) : CALL_NO_MEMORY;
  unsigned expected = 0;
  if (status == CALL_OK && !atomic_compare_exchange_strong_explicit(&gate->pending, &expected,
      NATIVE_COMPLETION_PER_PROCESS_LIMIT, memory_order_acq_rel, memory_order_acquire)) {
    status = CALL_BUSY;
  }
  struct native_completion *operation = NULL;
  if (status == CALL_OK) {
    operation = kmalloc(sizeof(*operation));
    if (!operation) {
      atomic_store_explicit(&gate->pending, 0, memory_order_release);
      status = CALL_NO_MEMORY;
    } else if (!object_retain(&gate->storage)) {
      kfree(operation);
      operation = NULL;
      atomic_store_explicit(&gate->pending, 0, memory_order_release);
      status = CALL_LIMIT;
    }
  }
  if (operation) {
    *operation = (struct native_completion){.gate = gate,
      .protocol = request->protocol, .operation = request->operation,
      .deadline_ns = request->deadline_ns, .correlation_id = request->correlation_id,
      .input_size = request->input_size, .target = request->target};
    request->target = (struct capability_reference){0};
    object_init(&operation->storage, OBJECT_COMPLETION_STORAGE, destroy_operation);
    atomic_init(&operation->control, 0);
    operation->cleanup_group = object_cleanup_defer();
    memcpy(operation->input, request->input, request->input_size);
    status = endpoint_completion_reserve((struct endpoint *)request->receiver.object,
        operation, &request->reservation, request->slots);
    if (status == CALL_OK) {
      status = reserve_backend(operation);
      if (status == CALL_OK) {
        status = endpoint_completion_commit(operation);
      }
      if (status != CALL_OK) {
        if (operation->backend) {
          discard_backend(operation);
        }
        endpoint_completion_discard(operation);
      }
    }
    if (status != CALL_OK) {
      atomic_store_explicit(&gate->pending, 0, memory_order_release);
      operation->delivery = NULL;
      object_release(&operation->storage);
      operation = NULL;
    }
  }
  capability_reservation_release(&request->reservation, request->slots);
  capability_release(&request->target);
  capability_release(&request->receiver);
  request->owner = NULL;
  request->result = status;
  if (operation) {
    struct execution_group *previous = object_cleanup_enter(operation->cleanup_group);
    start_backend(operation);
    object_cleanup_leave(previous);
  }
  bsp_request_complete(&request->request);
}

static enum call_status source_status(struct kernel_object *object, uint64_t rights,
    uint64_t protocol, uint64_t operation, uint64_t *maximum, size_t *grant_count)
{
  *maximum = UINT64_C(30000000000);
  *grant_count = 0;
  uint64_t required;
  switch (object->type) {
  case OBJECT_TCP_SERVICE:
    if (protocol != PROTOCOL_TCP_SERVICE || operation != TCP_CONNECT) {
      return CALL_BAD_OPERATION;
    }
    required = TCP_SERVICE_RIGHT_CONNECT;
    *grant_count = 1;
    break;
  case OBJECT_UDP:
    if (protocol != PROTOCOL_UDP || (operation != UDP_SEND && operation != UDP_RECEIVE)) {
      return CALL_BAD_OPERATION;
    }
    required = operation == UDP_SEND ? UDP_RIGHT_SEND : UDP_RIGHT_RECEIVE;
    *maximum = operation == UDP_SEND ? UDP_SEND_MAX_WAIT_NS : UDP_RECEIVE_MAX_WAIT_NS;
    break;
  case OBJECT_FILE:
    if (protocol != PROTOCOL_FILE) {
      return CALL_BAD_OPERATION;
    }
    if (operation == FILE_READ) {
      required = FILE_RIGHT_READ;
    } else if (operation == FILE_WRITE || operation == FILE_RESIZE) {
      required = FILE_RIGHT_WRITE;
    } else if (operation == FILE_SIZE) {
      required = rights & FILE_RIGHTS;
      if (!required) {
        return CALL_DENIED;
      }
    } else {
      return CALL_BAD_OPERATION;
    }
    break;
  case OBJECT_DIRECTORY:
    if (protocol != PROTOCOL_DIRECTORY ||
        (operation != DIRECTORY_CREATE && operation != DIRECTORY_REMOVE)) {
      return CALL_BAD_OPERATION;
    }
    required = operation == DIRECTORY_CREATE ? DIRECTORY_RIGHT_CREATE : DIRECTORY_RIGHT_REMOVE;
    *grant_count = operation == DIRECTORY_CREATE;
    break;
  default:
    return CALL_BAD_OPERATION;
  }
  return (rights & required) == required ? CALL_OK : CALL_DENIED;
}

struct syscall_result native_completion_call(struct kernel_object *target, uint64_t rights,
    uint64_t transport, uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  (void)reply_address;
  if (reply_capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct process *process = process_current();
  if (operation == COMPLETION_CANCEL) {
    uint64_t id;
    if (target->type != OBJECT_ENDPOINT_RECEIVER) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    if (!(transport & HANDLE_TRANSPORT_RECEIVE) || ((struct endpoint *)target)->owner != process) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size != sizeof(id)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&id, request_address, sizeof(id))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){endpoint_completion_cancel((struct endpoint *)target, id), 0};
  }
  if (operation != COMPLETION_SUBMIT || transport ||
      request_size != sizeof(struct completion_submit_message) - sizeof(struct message_header)) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct completion_submit_message input = {0};
  if (!copy_from_user((uint8_t *)&input + sizeof(input.header), request_address, request_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!input.correlation_id || input.size > COMPLETION_PAYLOAD_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t maximum;
  size_t grant_count;
  enum call_status status = source_status(target, rights, input.protocol, input.operation,
      &maximum, &grant_count);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (process_busy(process)) {
    return (struct syscall_result){CALL_BUSY, 0};
  }
  status = deadline_status(input.deadline_ns, maximum);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct capability_reference receiver;
  enum capability_result cap = capability_acquire(&process->capabilities, input.receiver, 0,
      HANDLE_TRANSPORT_RECEIVE, &receiver);
  if (cap != CAP_OK) {
    return (struct syscall_result){cap == CAP_DENIED ? CALL_DENIED :
        cap == CAP_LIMIT ? CALL_LIMIT : CALL_BAD_HANDLE, 0};
  }
  if (receiver.object->type != OBJECT_ENDPOINT_RECEIVER ||
      ((struct endpoint *)receiver.object)->owner != process) {
    capability_release(&receiver);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  cap = capability_request_reservation(grant_count + 1, &reservation, slots);
  if (cap != CAP_OK) {
    capability_release(&receiver);
    return (struct syscall_result){cap == CAP_LIMIT ? CALL_LIMIT :
        cap == CAP_DENIED ? CALL_DENIED : CALL_NO_MEMORY, 0};
  }
  if (task_stop_requested() || !object_retain(target)) {
    capability_reservation_release(&reservation, slots);
    capability_release(&receiver);
    return (struct syscall_result){task_stop_requested() ? CALL_ENDPOINT_CLOSED : CALL_LIMIT, 0};
  }
  struct completion_admit_request *request = (struct completion_admit_request *)
      bsp_request_prepare(BSP_SERVICE_COMPLETION);
  request->owner = process;
  request->receiver = receiver;
  request->target = (struct capability_reference){target, rights, 0};
  request->reservation = reservation;
  memcpy(request->slots, slots, (grant_count + 1) * sizeof(slots[0]));
  request->protocol = input.protocol;
  request->operation = input.operation;
  request->deadline_ns = input.deadline_ns;
  request->correlation_id = input.correlation_id;
  request->input_size = input.size;
  if (!copy_from_user(request->input, input.buffer, input.size)) {
    capability_reservation_release(&request->reservation, request->slots);
    capability_release(&request->target);
    capability_release(&request->receiver);
    bsp_request_cancel_prepared(&request->request);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  bsp_request_submit_and_wait(&request->request);
  status = request->result;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}
