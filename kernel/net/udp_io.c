#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/completion.h>
#include <kernel/memory.h>
#include <kernel/net/arp.h>
#include <kernel/net/storage.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include "udp_internal.h"
#include "wire.h"

#define UDP_IO_BUDGET 16
#define UDP_COMPLETION_BUDGET 16

enum udp_io_state { IO_RESERVED, IO_QUEUED, IO_RUNNING, IO_WAITING, IO_FINISHED, IO_DONE };
struct udp_io_queue;

struct udp_io {
  struct udp_io *next, *previous, *done_next;
  struct udp_io *token_next, *token_previous;
  struct udp_io_queue *queue;
  bool sending, token_indexed, scan_phase;
  enum udp_io_state state;
  struct udp_endpoint *endpoint;
  uint64_t token, deadline;
  uint32_t address;
  uint16_t port;
  size_t length; /* Send length or receive capacity until completion. */
  uint8_t data[UDP_MAX_PAYLOAD];
  struct udp_receive_reply reply;
  enum call_status status;
  struct task_wait *wait;
  struct forwarder_operation *context;
  struct native_completion *completion;
  bool cancelled;
};

/* Active calls are owned until worker completion; direction ownership remains
 * through synchronous collection or native result transfer. Queue cursors only
 * lend one node at a time and all publication/removal is constant work. */
struct udp_io_queue {
  struct udp_io *head, *tail, *cursor;
  uint64_t deadline, scan_deadline;
  bool scanning, scan_phase;
  atomic_bool changed;
};
static struct udp_io_queue sends = {.deadline = UINT64_MAX};
static struct udp_io_queue receives = {.deadline = UINT64_MAX};
static struct udp_io *finished, *finished_tail;
/* Only the worker indexes sends that entered transmission. Waiting entries own
 * an ARP packet, so the existing packet budget also bounds token lookup. */
static struct udp_io *transmitting;
static atomic_bool io_locked;
static uint64_t next_token;
static size_t queued_datagrams; /* Worker-owned, within the software packet budget. */
static struct {
  uint64_t queued, delivered, unbound, queue_full, no_memory;
} udp_receive_stats;

static void lock_io(void)
{
  while (atomic_exchange_explicit(&io_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_io(void)
{
  atomic_store_explicit(&io_locked, false, memory_order_release);
}

static enum call_status check_deadline(uint64_t deadline, uint64_t maximum)
{
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  return deadline - now > maximum ? CALL_BAD_REQUEST : CALL_OK;
}

/* IF=0, I/O lock held. */
static void publish_io(struct udp_io *call)
{
  struct udp_io_queue *queue = call->sending ? &sends : &receives;
  call->queue = queue;
  call->previous = queue->tail;
  if (queue->tail) {
    queue->tail->next = call;
  } else {
    queue->head = call;
  }
  queue->tail = call;
  /* New tail entries belong to the next sweep, so steady admission cannot
   * indefinitely postpone revisiting an older waiting request. */
  call->scan_phase = queue->scan_phase;
  atomic_store_explicit(&queue->changed, true, memory_order_release);
  queue->deadline = 0;
}

static void release_direction(struct udp_io *call)
{
  struct udp_io **direction = call->sending ? &call->endpoint->send : &call->endpoint->receive;
  KASSERT(*direction == call);
  *direction = NULL;
}

static void unlink_io(struct udp_io *call)
{
  struct udp_io_queue *queue = call->queue;
  KASSERT(queue);
  if (queue->cursor == call) {
    queue->cursor = call->next;
  }
  if (call->previous) {
    call->previous->next = call->next;
  } else {
    queue->head = call->next;
  }
  if (call->next) {
    call->next->previous = call->previous;
  } else {
    queue->tail = call->previous;
  }
  call->queue = NULL;
  call->next = call->previous = NULL;
}

static void unindex_send(struct udp_io *call)
{
  if (!call->token_indexed) {
    return;
  }
  if (call->token_previous) {
    call->token_previous->token_next = call->token_next;
  } else {
    transmitting = call->token_next;
  }
  if (call->token_next) {
    call->token_next->token_previous = call->token_previous;
  }
  call->token_indexed = false;
  call->token_next = call->token_previous = NULL;
}

static void complete_io(struct udp_io *call, enum call_status status)
{
  unindex_send(call);
  unlink_io(call);
  call->status = status;
  call->state = IO_DONE;
  if (call->completion) {
    if (finished_tail) {
      finished_tail->done_next = call;
    } else {
      finished = call;
    }
    finished_tail = call;
  }
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
  /* No access after unlock: a synchronous caller can collect immediately. */
}

/* Each worker pass resumes a bounded sweep. A partial sweep keeps deadline zero
 * so the worker yields and resumes before sleeping on its cached minimum. */
static void begin_scan(struct udp_io_queue *queue)
{
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  if (!queue->scanning) {
    atomic_store_explicit(&queue->changed, false, memory_order_release);
    queue->scan_phase = !queue->scan_phase;
    queue->cursor = queue->head;
    queue->scan_deadline = UINT64_MAX;
    queue->scanning = true;
  }
  unlock_io();
  cpu_restore_interrupts(flags);
}

/* I/O lock held. */
static struct udp_io *next_io(struct udp_io_queue *queue)
{
  struct udp_io *call = queue->cursor;
  if (call && call->scan_phase == queue->scan_phase) {
    call = NULL;
  }
  queue->cursor = call ? call->next : NULL;
  if (call) {
    call->scan_phase = queue->scan_phase;
  }
  return call;
}

static void finish_scan(struct udp_io_queue *queue)
{
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  if (!queue->cursor) {
    queue->scanning = false;
    queue->deadline = queue->scan_deadline;
  }
  unlock_io();
  cpu_restore_interrupts(flags);
}

/* I/O lock held, while this sweep still owns the call. */
static void wait_io(struct udp_io *call)
{
  call->state = IO_WAITING;
  if (call->deadline < call->queue->scan_deadline) {
    call->queue->scan_deadline = call->deadline;
  }
}

enum call_status net_udp_send(struct kernel_object *object, uint32_t address,
    uint16_t port, const uint8_t *data, size_t length, uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (length > UDP_MAX_PAYLOAD) {
    return CALL_LIMIT;
  }
  if (!port || (length && !data)) {
    return CALL_BAD_REQUEST;
  }
  enum call_status status = check_deadline(deadline, UDP_SEND_MAX_WAIT_NS);
  if (status != CALL_OK || !net_worker_available()) {
    return status != CALL_OK ? status : CALL_UNAVAILABLE;
  }
  struct udp_io *call = net_storage_alloc(sizeof(*call));
  if (!call) {
    return CALL_NO_MEMORY;
  }
  lock_io();
  struct udp_endpoint *endpoint = (struct udp_endpoint *)object;
  if (endpoint->send || next_token == UINT64_MAX) {
    status = endpoint->send ? CALL_BUSY : CALL_LIMIT;
    unlock_io();
    net_storage_free(call);
    return status;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct udp_io){
    .state = IO_QUEUED, .endpoint = endpoint, .sending = true,
    .token = ++next_token, .deadline = deadline, .address = address, .port = port,
    .length = length, .wait = wait, .context = task_forwarder_operation(),
  };
  memcpy(call->data, data, length);
  endpoint->send = call;
  publish_io(call);
  unlock_io();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_io();
  while (call->state != IO_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    atomic_store_explicit(&call->queue->changed, true, memory_order_release);
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_io();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_io();
  }
  KASSERT(!call->wait);
  status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  release_direction(call);
  unlock_io();
  net_storage_free(call);
  return status;
}

enum call_status net_udp_receive(struct kernel_object *object, size_t capacity,
    uint64_t deadline, uint8_t *data, struct udp_receive_reply *reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(capacity <= UDP_MAX_PAYLOAD);
  enum call_status status = check_deadline(deadline, UDP_RECEIVE_MAX_WAIT_NS);
  if (status != CALL_OK || !net_worker_available()) {
    return status != CALL_OK ? status : CALL_UNAVAILABLE;
  }
  struct udp_io *call = net_storage_alloc(sizeof(*call));
  if (!call) {
    return CALL_NO_MEMORY;
  }
  lock_io();
  struct udp_endpoint *endpoint = (struct udp_endpoint *)object;
  if (endpoint->receive) {
    unlock_io();
    net_storage_free(call);
    return CALL_BUSY;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct udp_io){
    .state = IO_QUEUED, .endpoint = endpoint,
    .deadline = deadline, .length = capacity, .wait = wait,
    .context = task_forwarder_operation(),
  };
  endpoint->receive = call;
  publish_io(call);
  unlock_io();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_io();
  while (call->state != IO_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    atomic_store_explicit(&call->queue->changed, true, memory_order_release);
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_io();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_io();
  }
  KASSERT(!call->wait);
  status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK) {
    *reply = call->reply;
    memcpy(data, call->data, reply->length);
  }
  release_direction(call);
  unlock_io();
  net_storage_free(call);
  return status;
}

enum call_status net_udp_completion_reserve(struct native_completion *operation)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  bool sending = operation->operation == UDP_SEND;
  if (operation->target.object->type != OBJECT_UDP ||
      (!sending && operation->operation != UDP_RECEIVE)) {
    return CALL_BAD_OPERATION;
  }
  uint64_t rights = sending ? UDP_RIGHT_SEND : UDP_RIGHT_RECEIVE;
  if (!(operation->target.rights & rights)) {
    return CALL_DENIED;
  }
  struct completion_udp_send_request request = {0};
  size_t length;
  if (sending) {
    if (operation->input_size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    if (request.reserved || !request.port ||
        request.size != operation->input_size - sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    if (request.size > UDP_MAX_PAYLOAD) {
      return CALL_LIMIT;
    }
    length = request.size;
  } else {
    struct completion_udp_receive_request receive;
    if (operation->input_size != sizeof(receive)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&receive, operation->input, sizeof(receive));
    length = receive.capacity < UDP_MAX_PAYLOAD ? receive.capacity : UDP_MAX_PAYLOAD;
  }
  enum call_status status = check_deadline(operation->deadline_ns,
      sending ? UDP_SEND_MAX_WAIT_NS : UDP_RECEIVE_MAX_WAIT_NS);
  if (status != CALL_OK || !net_worker_available()) {
    return status != CALL_OK ? status : CALL_UNAVAILABLE;
  }
  status = native_completion_charge(operation, sizeof(struct udp_io));
  if (status != CALL_OK) {
    return status;
  }
  struct udp_io *call = net_storage_alloc(sizeof(*call));
  if (!call) {
    return CALL_NO_MEMORY;
  }
  struct udp_endpoint *endpoint = (struct udp_endpoint *)operation->target.object;
  lock_io();
  if ((sending ? endpoint->send : endpoint->receive) ||
      (sending && next_token == UINT64_MAX)) {
    status = (sending ? endpoint->send : endpoint->receive) ? CALL_BUSY : CALL_LIMIT;
    unlock_io();
    net_storage_free(call);
    return status;
  }
  *call = (struct udp_io){
    .state = IO_RESERVED, .endpoint = endpoint, .sending = sending,
    .token = sending ? ++next_token : 0, .deadline = operation->deadline_ns,
    .address = request.address, .port = request.port, .length = length,
    .completion = operation, .context = &operation->lineage,
  };
  if (sending) {
    memcpy(call->data, operation->input + sizeof(request), length);
  }
  if (sending) {
    endpoint->send = call;
  } else {
    endpoint->receive = call;
  }
  operation->backend = call;
  unlock_io();
  return CALL_OK;
}

void net_udp_completion_start(struct native_completion *operation)
{
  lock_io();
  struct udp_io *call = operation->backend;
  KASSERT(call && call->state == IO_RESERVED && call->completion == operation);
  call->state = IO_QUEUED;
  publish_io(call);
  unlock_io();
  net_worker_notify();
}

void net_udp_completion_discard(struct native_completion *operation)
{
  lock_io();
  struct udp_io *call = operation->backend;
  KASSERT(call && call->state == IO_RESERVED && call->completion == operation);
  release_direction(call);
  operation->backend = NULL;
  unlock_io();
  net_storage_free(call);
}

static bool finish_completions(void)
{
  bool worked = false;
  for (unsigned i = 0; i < UDP_COMPLETION_BUDGET; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = finished;
    if (!call) {
      unlock_io();
      cpu_restore_interrupts(flags);
      break;
    }
    finished = call->done_next;
    if (!finished) {
      finished_tail = NULL;
    }
    struct native_completion *operation = call->completion;
    enum call_status status = call->status;
    uint8_t output[sizeof(struct udp_receive_reply) + UDP_MAX_PAYLOAD];
    size_t size = 0;
    if (status == CALL_OK) {
      operation->progress_known = true;
      operation->progress = call->sending ? call->length : call->reply.length;
      if (!call->sending) {
        memcpy(output, &call->reply, sizeof(call->reply));
        memcpy(output + sizeof(call->reply), call->data, call->reply.length);
        size = sizeof(call->reply) + call->reply.length;
      }
    }
    release_direction(call);
    operation->backend = NULL;
    unlock_io();
    net_storage_free(call);
    native_completion_finish(operation, status, size ? output : NULL, size, NULL);
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}

static enum call_status send_status(enum net_result result)
{
  switch (result) {
  case NET_OK: return CALL_OK;
  case NET_INVALID: return CALL_BAD_REQUEST;
  case NET_UNAVAILABLE: return CALL_UNAVAILABLE;
  case NET_QUEUE_FULL: return CALL_QUEUE_FULL;
  case NET_NO_ROUTE: return CALL_NO_ROUTE;
  case NET_NO_MEMORY: return CALL_NO_MEMORY;
  case NET_TIMED_OUT: return CALL_TIMED_OUT;
  }
  return CALL_UNAVAILABLE;
}

static void finish_transmission(uint64_t token, enum call_status status)
{
  /* This index is exclusively worker-owned; no process-sized IF=0 search. */
  struct udp_io *call = transmitting;
  while (call && call->token != token) {
    call = call->token_next;
  }
  if (!call) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  if (call->state == IO_RUNNING) {
    /* send_packet still borrows the payload until its stack returns. */
    call->status = status;
    call->state = IO_FINISHED;
  } else if (call->state == IO_WAITING) {
    complete_io(call, status);
  }
  unlock_io();
  cpu_restore_interrupts(flags);
}

void net_udp_transmitted(uint64_t token)
{
  finish_transmission(token, CALL_OK);
}

void net_udp_failed(uint64_t token, enum net_result result)
{
  finish_transmission(token, send_status(result));
}

static void release_datagram(struct udp_endpoint *endpoint)
{
  struct udp_datagram *datagram = &endpoint->received[endpoint->receive_head];
  uint64_t flags = cpu_save_interrupts();
  size_t charge = sizeof(*datagram->packet) + datagram->packet->length;
  net_packet_release(datagram->packet);
  net_storage_uncharge(charge);
  cpu_restore_interrupts(flags);
  *datagram = (struct udp_datagram){0};
  endpoint->receive_head = (endpoint->receive_head + 1) % UDP_RECEIVE_QUEUE_LIMIT;
  --endpoint->receive_count;
  --queued_datagrams;
}

void net_udp_discard_received(struct udp_endpoint *endpoint)
{
  while (endpoint->receive_count) {
    release_datagram(endpoint);
  }
}

/* Worker only, outside io lock. Caller cannot release an active call. Cancel
 * the ARP packet before waking, without an ARP callback reentering io state. */
static void cancel_send(struct udp_io *call, enum call_status status)
{
  net_arp_cancel((struct ipv4_completion){IPV4_NOTIFY_UDP, call->token});
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  complete_io(call, status);
  unlock_io();
  cpu_restore_interrupts(flags);
}

void net_udp_stop_io(struct udp_endpoint *endpoint, enum call_status status)
{
  net_udp_discard_received(endpoint);
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  struct udp_io *call = endpoint->send;
  bool cancel = call && (call->state == IO_QUEUED || call->state == IO_WAITING);
  unlock_io();
  cpu_restore_interrupts(flags);
  if (cancel) {
    cancel_send(call, status);
  }
  flags = cpu_save_interrupts();
  lock_io();
  call = endpoint->receive;
  if (call && (call->state == IO_QUEUED || call->state == IO_WAITING)) {
    complete_io(call, status);
  }
  unlock_io();
  cpu_restore_interrupts(flags);
}

/* The worker lends this active node until its final locked transition. */
static bool service_receive(struct udp_io *call)
{
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  if (call->state != IO_QUEUED && call->state != IO_WAITING) {
    unlock_io();
    cpu_restore_interrupts(flags);
    return false;
  }
  bool cancelled = call->cancelled;
  call->state = IO_RUNNING;
  unlock_io();
  cpu_restore_interrupts(flags);

  struct udp_endpoint *endpoint = call->endpoint;
  enum call_status status = CALL_OK;
  if (call->completion) {
    bool started = !endpoint->receive_count ||
        native_completion_mark_started(call->completion);
    status = native_completion_cancel_reason(call->completion);
    KASSERT(started || status != CALL_OK);
  } else if (cancelled) {
    status = CALL_ENDPOINT_CLOSED;
  }
  flags = cpu_save_interrupts();
  bool denied = forwarder_operation_denied(call->context);
  if (status == CALL_OK && denied) {
    status = CALL_DENIED;
    if (call->completion) {
      native_completion_request_cancel(call->completion, CALL_DENIED);
    }
  }
  cpu_restore_interrupts(flags);
  if (status == CALL_OK) {
    status = net_udp_endpoint_status(endpoint);
  }
  if (status == CALL_OK && task_deadline_expired(call->deadline)) {
    status = CALL_TIMED_OUT;
  }
  bool complete = status != CALL_OK || endpoint->receive_count;
  if (status == CALL_OK && endpoint->receive_count) {
    flags = cpu_save_interrupts();
    bool started = forwarder_operation_start(call->context);
    cpu_restore_interrupts(flags);
    if (!started) {
      status = CALL_DENIED;
    }
  }
  if (status == CALL_OK && endpoint->receive_count) {
    struct udp_datagram *datagram = &endpoint->received[endpoint->receive_head];
    size_t length = datagram->packet->length - UDP_HEADER_SIZE;
    if (length > call->length) {
      status = CALL_BUFFER_TOO_SMALL;
    } else {
      call->reply = (struct udp_receive_reply){
        .address = datagram->source, .port = net_read_u16(datagram->packet->data), .length = length,
      };
      memcpy(call->data, datagram->packet->data + UDP_HEADER_SIZE, length);
      release_datagram(endpoint);
      ++udp_receive_stats.delivered;
    }
  }
  flags = cpu_save_interrupts();
  lock_io();
  if (complete) {
    complete_io(call, status);
  } else {
    wait_io(call);
  }
  unlock_io();
  cpu_restore_interrupts(flags);
  return complete;
}

static bool service_receives(void)
{
  bool worked = false;
  begin_scan(&receives);
  for (unsigned i = 0; i < UDP_IO_BUDGET; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = next_io(&receives);
    unlock_io();
    cpu_restore_interrupts(flags);
    if (!call) {
      break;
    }
    worked |= service_receive(call);
  }
  finish_scan(&receives);
  return worked;
}

bool net_udp_service_io(void)
{
  bool worked = service_receives();
  begin_scan(&sends);
  for (unsigned i = 0; i < UDP_IO_BUDGET; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = next_io(&sends);
    if (!call) {
      unlock_io();
      cpu_restore_interrupts(flags);
      break;
    }
    bool active = call->state == IO_QUEUED || call->state == IO_WAITING;
    enum call_status cancellation = CALL_OK;
    if (active && call->completion) {
      if (task_deadline_expired(call->deadline)) {
        native_completion_request_cancel(call->completion, CALL_TIMED_OUT);
      }
      cancellation = native_completion_cancel_reason(call->completion);
    } else if (active && (call->cancelled || task_deadline_expired(call->deadline))) {
      cancellation = call->cancelled ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT;
    }
    if (active && cancellation != CALL_OK) {
      unlock_io();
      cpu_restore_interrupts(flags);
      cancel_send(call, cancellation);
      worked = true;
      continue;
    }
    if (call->state != IO_QUEUED) {
      if (call->state == IO_WAITING) {
        wait_io(call);
      }
      unlock_io();
      cpu_restore_interrupts(flags);
      continue;
    }
    bool cancelled = call->cancelled;
    call->state = IO_RUNNING;
    unlock_io();
    cpu_restore_interrupts(flags);

    enum call_status status = CALL_OK;
    if (call->completion && !native_completion_mark_started(call->completion)) {
      status = native_completion_cancel_reason(call->completion);
    } else {
      status = cancelled ? CALL_ENDPOINT_CLOSED : net_udp_endpoint_status(call->endpoint);
    }
    if (status == CALL_OK) {
      flags = cpu_save_interrupts();
      bool started = forwarder_operation_start(call->context);
      cpu_restore_interrupts(flags);
      if (!started) {
        status = CALL_DENIED;
      }
    }
    if (status == CALL_OK) {
      call->token_next = transmitting;
      if (transmitting) {
        transmitting->token_previous = call;
      }
      transmitting = call;
      call->token_indexed = true;
      status = send_status(net_udp_send_packet(call->endpoint, call->address,
          call->port, call->data, call->length, call->deadline, call->token));
    }
    flags = cpu_save_interrupts();
    lock_io();
    if (status != CALL_OK) {
      complete_io(call, status);
    } else if (call->state == IO_FINISHED) {
      complete_io(call, call->status);
    } else {
      KASSERT(call->state == IO_RUNNING);
      wait_io(call);
    }
    unlock_io();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  finish_scan(&sends);
  worked |= finish_completions();
  return worked;
}

void net_udp_deliver(const struct net_interface *interface, uint32_t source,
    uint32_t destination, uint16_t port,
    const uint8_t *message, size_t length)
{
  struct udp_endpoint *endpoint = net_udp_find_receiver(interface, destination, port);
  if (!endpoint) {
    ++udp_receive_stats.unbound;
    return;
  }
  if (endpoint->receive_count == UDP_RECEIVE_QUEUE_LIMIT || queued_datagrams == UDP_RECEIVE_GLOBAL_LIMIT) {
    ++udp_receive_stats.queue_full;
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  size_t charge = sizeof(struct net_packet) + length;
  bool charged = net_storage_charge(charge);
  struct net_packet *packet = charged ? net_packet_allocate(length) : NULL;
  if (!packet && charged) {
    net_storage_uncharge(charge);
  }
  cpu_restore_interrupts(flags);
  if (!packet) {
    ++udp_receive_stats.no_memory;
    return;
  }
  memcpy(packet->data, message, length);
  size_t tail = (endpoint->receive_head + endpoint->receive_count) % UDP_RECEIVE_QUEUE_LIMIT;
  endpoint->received[tail] = (struct udp_datagram){.packet = packet, .source = source};
  ++endpoint->receive_count;
  ++queued_datagrams;
  ++udp_receive_stats.queued;
  /* RX can run after the worker's I/O pass; satisfy parked receivers now rather
   * than sleeping until their deadlines with a datagram already available. */
  uint64_t service_flags = cpu_save_interrupts();
  lock_io();
  struct udp_io *call = endpoint->receive;
  if (call && call->state != IO_QUEUED && call->state != IO_WAITING) {
    call = NULL;
  }
  unlock_io();
  cpu_restore_interrupts(service_flags);
  if (call) {
    service_receive(call);
  }
}

void net_udp_changed(void)
{
  atomic_store_explicit(&sends.changed, true, memory_order_release);
  atomic_store_explicit(&receives.changed, true, memory_order_release);
}

/* All scans are resumed by worker turns; this IF=0 query is constant work. */
bool net_udp_next_deadline(uint64_t *deadline)
{
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  uint64_t sending = sends.scanning ||
      atomic_load_explicit(&sends.changed, memory_order_acquire) ? 0 : sends.deadline;
  uint64_t receiving = receives.scanning ||
      atomic_load_explicit(&receives.changed, memory_order_acquire) ? 0 : receives.deadline;
  *deadline = finished ? 0 : sending < receiving ? sending : receiving;
  bool found = *deadline != UINT64_MAX;
  unlock_io();
  cpu_restore_interrupts(flags);
  return found;
}
