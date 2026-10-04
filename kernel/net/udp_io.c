#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/arp.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include "udp_internal.h"
#include "wire.h"

#define UDP_SEND_LIMIT 8
#define UDP_RECEIVE_LIMIT 16

enum udp_io_state { IO_FREE, IO_QUEUED, IO_RUNNING, IO_WAITING, IO_FINISHED, IO_DONE };
struct udp_io {
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
  bool cancelled;
};

/* Separate bounded copies let AP callers sleep without lending private memory.
 * DONE retains its slot and per-direction exclusion until the caller resumes. */
static struct udp_io sends[UDP_SEND_LIMIT], receives[UDP_RECEIVE_LIMIT];
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

/* IF=0, lock held. A live grant pins endpoint across publication and parking. */
static struct udp_io *reserve_io(struct udp_io *calls, size_t count,
    struct udp_endpoint *endpoint, enum call_status *status)
{
  struct udp_io *free_call = NULL;
  for (size_t i = 0; i < count; ++i) {
    if (calls[i].state != IO_FREE && calls[i].endpoint == endpoint) {
      *status = CALL_BUSY;
      return NULL;
    }
    if (calls[i].state == IO_FREE && !free_call) {
      free_call = &calls[i];
    }
  }
  *status = free_call ? CALL_OK : CALL_QUEUE_FULL;
  return free_call;
}

static void complete_io(struct udp_io *call, enum call_status status)
{
  call->status = status;
  call->state = IO_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
  /* Caller may immediately consume/reuse this slot after the lock is released. */
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
  lock_io();
  struct udp_io *call = reserve_io(sends, UDP_SEND_LIMIT, (struct udp_endpoint *)object, &status);
  if (!call || next_token == UINT64_MAX) {
    unlock_io();
    return call ? CALL_LIMIT : status;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct udp_io){
    .state = IO_QUEUED, .endpoint = (struct udp_endpoint *)object,
    .token = ++next_token, .deadline = deadline, .address = address, .port = port,
    .length = length, .wait = wait,
  };
  memcpy(call->data, data, length);
  unlock_io();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_io();
  while (call->state != IO_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_io();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_io();
  }
  KASSERT(!call->wait);
  status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  *call = (struct udp_io){0};
  unlock_io();
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
  lock_io();
  struct udp_io *call = reserve_io(receives, UDP_RECEIVE_LIMIT, (struct udp_endpoint *)object, &status);
  if (!call) {
    unlock_io();
    return status;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct udp_io){
    .state = IO_QUEUED, .endpoint = (struct udp_endpoint *)object,
    .deadline = deadline, .length = capacity, .wait = wait,
  };
  unlock_io();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_io();
  while (call->state != IO_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
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
  *call = (struct udp_io){0};
  unlock_io();
  return status;
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
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  for (size_t i = 0; i < UDP_SEND_LIMIT; ++i) {
    struct udp_io *call = &sends[i];
    if (call->token != token) {
      continue;
    }
    if (call->state == IO_RUNNING) {
      /* Immediate acceptance can occur inside send_packet. Defer wake until
       * that stack has stopped borrowing the slot's payload and endpoint. */
      call->status = status;
      call->state = IO_FINISHED;
    } else if (call->state == IO_WAITING) {
      complete_io(call, status);
    }
    break;
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

static enum call_status endpoint_status(const struct udp_endpoint *endpoint)
{
  switch (endpoint->local.state) {
  case UDP_STATE_BOUND: return CALL_OK;
  case UDP_STATE_SHUTDOWN: return CALL_ENDPOINT_CLOSED;
  default: return CALL_UNAVAILABLE;
  }
}

static void release_datagram(struct udp_endpoint *endpoint)
{
  struct udp_datagram *datagram = &endpoint->received[endpoint->receive_head];
  uint64_t flags = cpu_save_interrupts();
  net_packet_release(datagram->packet);
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
  for (size_t i = 0; i < UDP_SEND_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = &sends[i];
    bool cancel = call->endpoint == endpoint &&
        (call->state == IO_QUEUED || call->state == IO_WAITING);
    unlock_io();
    cpu_restore_interrupts(flags);
    if (cancel) {
      cancel_send(call, status);
    }
  }
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  for (size_t i = 0; i < UDP_RECEIVE_LIMIT; ++i) {
    struct udp_io *call = &receives[i];
    if (call->endpoint == endpoint && (call->state == IO_QUEUED || call->state == IO_WAITING)) {
      complete_io(call, status);
    }
  }
  unlock_io();
  cpu_restore_interrupts(flags);
}

static bool service_receives(void)
{
  bool worked = false;
  for (size_t i = 0; i < UDP_RECEIVE_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = &receives[i];
    if (call->state != IO_QUEUED && call->state != IO_WAITING) {
      unlock_io();
      cpu_restore_interrupts(flags);
      continue;
    }
    bool cancelled = call->cancelled;
    call->state = IO_RUNNING;
    unlock_io();
    cpu_restore_interrupts(flags);

    struct udp_endpoint *endpoint = call->endpoint;
    enum call_status status = cancelled ? CALL_ENDPOINT_CLOSED : endpoint_status(endpoint);
    if (status == CALL_OK && task_deadline_expired(call->deadline)) {
      status = CALL_TIMED_OUT;
    }
    bool complete = status != CALL_OK || endpoint->receive_count;
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
      worked = true;
    } else {
      call->state = IO_WAITING;
    }
    unlock_io();
    cpu_restore_interrupts(flags);
  }
  return worked;
}

bool net_udp_service_io(void)
{
  bool worked = service_receives();
  for (size_t i = 0; i < UDP_SEND_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_io();
    struct udp_io *call = &sends[i];
    bool active = call->state == IO_QUEUED || call->state == IO_WAITING;
    if (active && (call->cancelled || task_deadline_expired(call->deadline))) {
      enum call_status status = call->cancelled ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT;
      unlock_io();
      cpu_restore_interrupts(flags);
      cancel_send(call, status);
      worked = true;
      continue;
    }
    if (call->state != IO_QUEUED) {
      unlock_io();
      cpu_restore_interrupts(flags);
      continue;
    }
    bool cancelled = call->cancelled;
    call->state = IO_RUNNING;
    unlock_io();
    cpu_restore_interrupts(flags);

    enum call_status status = cancelled ? CALL_ENDPOINT_CLOSED : endpoint_status(call->endpoint);
    if (status == CALL_OK) {
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
      call->state = IO_WAITING;
    }
    unlock_io();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}

void net_udp_deliver(uint32_t source, uint32_t destination, uint16_t port,
    const uint8_t *message, size_t length)
{
  struct udp_endpoint *endpoint = net_udp_find_receiver(destination, port);
  if (!endpoint) {
    ++udp_receive_stats.unbound;
    return;
  }
  if (endpoint->receive_count == UDP_RECEIVE_QUEUE_LIMIT || queued_datagrams == UDP_RECEIVE_GLOBAL_LIMIT) {
    ++udp_receive_stats.queue_full;
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  struct net_packet *packet = net_packet_allocate(length);
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
  service_receives();
}

/* io lock held. QUEUED must run before the worker sleeps; WAITING contributes
 * its application deadline. Completed slots no longer need a timer wake. */
static bool io_next_deadline(const struct udp_io *calls, size_t count, uint64_t *deadline)
{
  bool found = false;
  for (size_t i = 0; i < count; ++i) {
    const struct udp_io *call = &calls[i];
    if (call->state == IO_QUEUED || call->state == IO_WAITING) {
      uint64_t next = call->state == IO_QUEUED ? 0 : call->deadline;
      if (next < *deadline) {
        *deadline = next;
      }
      found = true;
    }
  }
  return found;
}

bool net_udp_next_deadline(uint64_t *deadline)
{
  *deadline = UINT64_MAX;
  uint64_t flags = cpu_save_interrupts();
  lock_io();
  bool found = io_next_deadline(sends, UDP_SEND_LIMIT, deadline);
  found |= io_next_deadline(receives, UDP_RECEIVE_LIMIT, deadline);
  unlock_io();
  cpu_restore_interrupts(flags);
  return found;
}
