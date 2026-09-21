#include <abi/endpoint.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

struct endpoint_request {
  struct task_wait wait;
  struct endpoint_packet message, reply;
  enum call_status status;
  bool delivered;
};

struct endpoint_state {
  struct endpoint_request *incoming; /* Borrowed from the blocked caller's stack. */
  struct task_wait *receiver;
  uint64_t next_id;
  bool closed;
};

struct endpoint_pair {
  atomic_bool locked;
  struct endpoint ends[2];
  struct endpoint_state state[2];
};

/* IF=0. Lock order is endpoint -> scheduler queues. No allocation, user copy
 * or context switch while held. Each published stack pointer is removed before
 * wake, since another CPU can immediately resume and discard that stack frame. */
static void lock_pair(struct endpoint_pair *pair)
{
  while (atomic_exchange_explicit(&pair->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pair(struct endpoint_pair *pair)
{
  atomic_store_explicit(&pair->locked, false, memory_order_release);
}

static void wake_receiver(struct endpoint_state *state)
{
  struct task_wait *wait = state->receiver;
  state->receiver = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void destroy_endpoint(struct kernel_object *object)
{
  struct endpoint *endpoint = (struct endpoint *)object;
  struct endpoint_pair *pair = endpoint->pair;
  struct endpoint_state *own = &pair->state[endpoint->side];
  struct endpoint_state *peer = &pair->state[endpoint->side ^ 1];

  lock_pair(pair);
  /* A blocked operation retains this end through its process's handle. */
  KASSERT(!own->closed && !own->receiver && !peer->incoming);
  own->closed = true;
  struct endpoint_request *request = own->incoming;
  own->incoming = NULL;
  if (request) {
    request->status = CALL_ENDPOINT_CLOSED;
    task_wait_wake(&request->wait);
  }
  wake_receiver(peer);
  bool both_closed = peer->closed;
  unlock_pair(pair);

  if (both_closed) {
    kfree(pair);
  }
}

bool endpoint_pair_create(struct endpoint **first, struct endpoint **second)
{
  KASSERT(arch_cpu_index() == 0 && first && second && first != second);
  *first = NULL;
  *second = NULL;
  struct endpoint_pair *pair = kmalloc(sizeof(*pair));
  if (!pair) {
    return false;
  }
  memset(pair, 0, sizeof(*pair));
  atomic_init(&pair->locked, false);
  for (unsigned i = 0; i < 2; ++i) {
    object_init(&pair->ends[i].object, OBJECT_ENDPOINT, destroy_endpoint);
    pair->ends[i].pair = pair;
    pair->ends[i].side = i;
    pair->state[i].next_id = 1;
  }
  *first = &pair->ends[0];
  *second = &pair->ends[1];
  return true;
}

static enum call_status call_peer(struct endpoint *endpoint,
    const union endpoint_payload *payload, struct endpoint_packet *reply)
{
  struct endpoint_request request = {0};
  request.message.size = payload->call.size;
  memcpy(request.message.data, payload->call.data, payload->call.size);
  task_wait_init(&request.wait);
  struct endpoint_pair *pair = endpoint->pair;
  struct endpoint_state *peer = &pair->state[endpoint->side ^ 1];

  lock_pair(pair);
  if (peer->closed) {
    unlock_pair(pair);
    return CALL_ENDPOINT_CLOSED;
  }
  if (peer->incoming) {
    unlock_pair(pair);
    return CALL_QUEUE_FULL;
  }
  if (!peer->next_id) {
    unlock_pair(pair);
    return CALL_UNAVAILABLE;
  }
  request.message.id = peer->next_id++;
  peer->incoming = &request;
  wake_receiver(peer);
  unlock_pair(pair);

  task_wait_sleep(&request.wait);
  /* Reacquire the resource lock before reading the completed response. No
   * peer touches request after detaching it and waking us under this lock. */
  lock_pair(pair);
  enum call_status status = request.status;
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  unlock_pair(pair);
  return status;
}

static enum call_status receive_request(struct endpoint *endpoint,
    struct endpoint_packet *reply)
{
  struct endpoint_pair *pair = endpoint->pair;
  struct endpoint_state *own = &pair->state[endpoint->side];
  struct endpoint_state *peer = &pair->state[endpoint->side ^ 1];

  for (;;) {
    struct task_wait wait;
    task_wait_init(&wait);
    lock_pair(pair);
    if (peer->closed) {
      unlock_pair(pair);
      return CALL_ENDPOINT_CLOSED;
    }
    if (own->incoming) {
      if (own->incoming->delivered) {
        unlock_pair(pair);
        return CALL_BUSY;
      }
      own->incoming->delivered = true;
      *reply = own->incoming->message;
      unlock_pair(pair);
      return CALL_OK;
    }
    if (own->receiver) {
      unlock_pair(pair);
      return CALL_BUSY;
    }
    own->receiver = &wait;
    unlock_pair(pair);
    task_wait_sleep(&wait);
  }
}

static enum call_status reply_to_request(struct endpoint *endpoint,
    const struct endpoint_packet *reply)
{
  struct endpoint_pair *pair = endpoint->pair;
  struct endpoint_state *own = &pair->state[endpoint->side];
  lock_pair(pair);
  if (pair->state[endpoint->side ^ 1].closed) {
    unlock_pair(pair);
    return CALL_ENDPOINT_CLOSED;
  }
  struct endpoint_request *request = own->incoming;
  if (!request || !request->delivered || reply->id != request->message.id) {
    unlock_pair(pair);
    return CALL_BAD_REQUEST;
  }
  request->reply.id = reply->id;
  request->reply.size = reply->size;
  memcpy(request->reply.data, reply->data, reply->size);
  request->status = CALL_OK;
  own->incoming = NULL;
  task_wait_wake(&request->wait);
  unlock_pair(pair);
  return CALL_OK;
}

struct syscall_result endpoint_call(struct endpoint *endpoint, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case ENDPOINT_CALL:
    required = ENDPOINT_RIGHT_CALL;
    break;
  case ENDPOINT_RECEIVE:
    required = ENDPOINT_RIGHT_RECEIVE;
    break;
  case ENDPOINT_REPLY:
    required = ENDPOINT_RIGHT_REPLY;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  union endpoint_payload payload;
  if (request_size != sizeof(payload)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&payload, request_address, sizeof(payload))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if ((operation == ENDPOINT_CALL && payload.call.size > ENDPOINT_DATA_MAX) ||
      (operation == ENDPOINT_REPLY && payload.reply.size > ENDPOINT_DATA_MAX)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == ENDPOINT_REPLY) {
    return (struct syscall_result){reply_to_request(endpoint, &payload.reply), 0};
  }

  struct endpoint_packet reply = {0};
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = operation == ENDPOINT_CALL ?
      call_peer(endpoint, &payload, &reply) : receive_request(endpoint, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The task resumes on its owning CPU/root; no other task can mutate its
   * private mappings while asleep. Only now touch its user reply buffer. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
