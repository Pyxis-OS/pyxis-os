#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/echo.h>
#include <kernel/net/icmp.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include "wire.h"

#define ECHO_PENDING_LIMIT 16

enum echo_state { ECHO_FREE, ECHO_QUEUED, ECHO_SENDING, ECHO_SENT, ECHO_DONE };

struct pending_echo {
  enum echo_state state;
  uint32_t destination;
  uint64_t token, deadline, started;
  struct task_wait *wait;
  enum call_status status;
  struct echo_reply reply;
};

/* Slots live in shared kernel memory; AP callers never lend their stacks or
 * address spaces to the worker. DONE remains reserved until its caller resumes. */
static struct pending_echo pending[ECHO_PENDING_LIMIT];
static atomic_bool pending_locked;
static uint64_t next_token;

static void lock_pending(void)
{
  while (atomic_exchange_explicit(&pending_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pending(void)
{
  atomic_store_explicit(&pending_locked, false, memory_order_release);
}

static void echo_payload(uint8_t payload[ECHO_PAYLOAD_BYTES], uint64_t token)
{
  memset(payload, 0, ECHO_PAYLOAD_BYTES);
  net_write_u32(payload, token >> 32);
  net_write_u32(payload + 4, token);
}

/* IF=0, pending lock held. Detach and wake under the lock before the caller
 * may consume/free the slot or reuse its task wait record. */
static void complete_echo(struct pending_echo *request, enum call_status status)
{
  request->status = status;
  request->state = ECHO_DONE;
  struct task_wait *wait = request->wait;
  request->wait = NULL;
  task_wait_wake(wait);
}

enum call_status net_echo_exchange(uint32_t destination, uint64_t deadline,
    struct echo_reply *reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (deadline - now > ECHO_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }

  lock_pending();
  struct pending_echo *request = NULL;
  for (size_t i = 0; i < ECHO_PENDING_LIMIT; ++i) {
    if (pending[i].state == ECHO_FREE) {
      request = &pending[i];
      break;
    }
  }
  if (!request || next_token == UINT64_MAX) {
    unlock_pending();
    return request ? CALL_LIMIT : CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *request = (struct pending_echo){
    .state = ECHO_QUEUED, .destination = destination,
    .token = ++next_token, .deadline = deadline, .wait = wait,
  };
  unlock_pending();
  net_worker_notify();
  task_wait_sleep(wait);

  lock_pending();
  KASSERT(request->state == ECHO_DONE && !request->wait);
  enum call_status status = request->status;
  if (status == CALL_OK) {
    *reply = request->reply;
  }
  request->state = ECHO_FREE;
  unlock_pending();
  return status;
}

static enum call_status echo_send_status(enum net_result result)
{
  switch (result) {
  case NET_OK: return CALL_OK;
  case NET_NO_ROUTE: return CALL_NO_ROUTE;
  case NET_NO_MEMORY: return CALL_NO_MEMORY;
  case NET_QUEUE_FULL: return CALL_QUEUE_FULL;
  case NET_UNAVAILABLE: return CALL_UNAVAILABLE;
  case NET_INVALID: return CALL_BAD_REQUEST;
  }
  return CALL_UNAVAILABLE;
}

bool net_echo_service(void)
{
  bool worked = false;
  for (size_t i = 0; i < ECHO_PENDING_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_pending();
    struct pending_echo *request = &pending[i];
    if ((request->state == ECHO_QUEUED || request->state == ECHO_SENT) &&
        task_deadline_expired(request->deadline)) {
      complete_echo(request, CALL_TIMED_OUT);
      worked = true;
    }
    if (request->state != ECHO_QUEUED) {
      unlock_pending();
      cpu_restore_interrupts(flags);
      continue;
    }
    worked = true;
    request->state = ECHO_SENDING;
    /* Only this worker can complete SENDING. The caller stays parked; no
     * pending lock is held across allocation, transmission or worker wakeup. */
    unlock_pending();
    uint8_t payload[ECHO_PAYLOAD_BYTES];
    echo_payload(payload, request->token);
    request->started = arch_monotonic_ns();
    enum net_result sent = net_icmp_echo_send(request->destination,
        request->token >> 16, request->token, payload, sizeof(payload));
    lock_pending();
    if (sent == NET_OK) {
      request->state = ECHO_SENT;
    } else {
      complete_echo(request, echo_send_status(sent));
    }
    unlock_pending();
    cpu_restore_interrupts(flags);
  }
  return worked;
}

bool net_echo_next_deadline(uint64_t *deadline)
{
  bool active = false;
  *deadline = UINT64_MAX;
  lock_pending();
  for (size_t i = 0; i < ECHO_PENDING_LIMIT; ++i) {
    if (pending[i].state == ECHO_QUEUED) {
      *deadline = 0;
      active = true;
      break;
    }
    if (pending[i].state == ECHO_SENT) {
      active = true;
      if (pending[i].deadline < *deadline) {
        *deadline = pending[i].deadline;
      }
    }
  }
  unlock_pending();
  return active;
}

void net_echo_receive(uint32_t source, uint32_t destination, uint16_t identifier,
    uint16_t sequence, const uint8_t *payload, size_t length)
{
  if (destination != IPV4_LOOPBACK_ADDRESS || length != ECHO_PAYLOAD_BYTES) {
    return;
  }
  uint64_t token = (uint64_t)net_read_u32(payload) << 32 | net_read_u32(payload + 4);
  uint8_t expected[ECHO_PAYLOAD_BYTES];
  echo_payload(expected, token);
  if (memcmp(expected, payload, sizeof(expected))) {
    return;
  }

  uint64_t flags = cpu_save_interrupts();
  lock_pending();
  for (size_t i = 0; i < ECHO_PENDING_LIMIT; ++i) {
    struct pending_echo *request = &pending[i];
    if (request->state != ECHO_SENT || request->destination != source ||
        request->token != token || identifier != (uint16_t)(token >> 16) ||
        sequence != (uint16_t)token) {
      continue;
    }
    uint64_t now = arch_monotonic_ns();
    if (now >= request->deadline) {
      complete_echo(request, CALL_TIMED_OUT);
    } else {
      request->reply = (struct echo_reply){
        .round_trip_ns = now - request->started, .address = source,
        .identifier = identifier, .sequence = sequence,
      };
      complete_echo(request, CALL_OK);
    }
    break;
  }
  unlock_pending();
  cpu_restore_interrupts(flags);
}
