#include <kernel/object/forwarder.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/net/storage.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/driver.h>
#include <kernel/net/udp.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include "udp_internal.h"

#define UDP_CONTROL_BUDGET 8
#define UDP_RETIRE_BUDGET 16
#define UDP_INVALIDATE_BUDGET 16
#define UDP_EPHEMERAL_FIRST 49152
#define UDP_EPHEMERAL_LAST 65535

enum udp_control_operation { CONTROL_OPEN, CONTROL_OPEN_ROUTE, CONTROL_OPEN_BROADCAST, CONTROL_INSPECT, CONTROL_SHUTDOWN };
enum udp_control_state { CONTROL_QUEUED, CONTROL_RUNNING, CONTROL_DONE };
struct udp_control {
  struct udp_control *next;
  enum udp_control_state state;
  enum udp_control_operation operation;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  struct capability_grant grant;
  struct net_factory *factory; /* Owned until caller collection. */
  struct net_path path;
  const struct net_interface *pinned;
  struct udp_endpoint *endpoint;
  uint32_t address;
  uint16_t port;
  struct udp_open_reply reply;
  enum call_status status;
  struct task_wait *wait;
  struct forwarder_operation *context;
  bool cancelled;
};

/* The worker owns the live list and all binding state. Control publication and
 * final-close handoff have their own lock, never held across allocation. */
static struct udp_endpoint *endpoints;
static struct udp_endpoint *invalidation_cursor;
static uint16_t next_ephemeral = UDP_EPHEMERAL_FIRST;
static struct udp_control *pending, *pending_tail;
static struct udp_endpoint *retired;
static atomic_bool control_locked;

static void lock_control(void)
{
  while (atomic_exchange_explicit(&control_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_control(void)
{
  atomic_store_explicit(&control_locked, false, memory_order_release);
}

static void assert_worker_context(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

/* Object retirement owns the zero-reference allocation until this handoff.
 * No slot allocation or blocking is needed, even when all calls are occupied. */
static void retire_endpoint(struct kernel_object *object)
{
  struct udp_endpoint *endpoint = (struct udp_endpoint *)object;
  endpoint->cleanup_group = object_cleanup_defer();
  lock_control();
  endpoint->retired_next = retired;
  retired = endpoint;
  unlock_control();
  net_worker_notify();
}

static bool reap_endpoints(void)
{
  bool worked = false;
  for (unsigned i = 0; i < UDP_RETIRE_BUDGET; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_control();
    struct udp_endpoint *endpoint = retired;
    if (endpoint) {
      retired = endpoint->retired_next;
    }
    unlock_control();
    cpu_restore_interrupts(flags);
    if (!endpoint) {
      break;
    }
    flags = cpu_save_interrupts();
    struct execution_group *group = endpoint->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    cpu_restore_interrupts(flags);
    if (invalidation_cursor == endpoint) {
      invalidation_cursor = endpoint->next;
    }
    /* Failed grant preflight can retire an unpublished endpoint too. */
    if (endpoint->previous) {
      endpoint->previous->next = endpoint->next;
    } else if (endpoints == endpoint) {
      endpoints = endpoint->next;
    }
    if (endpoint->next) {
      endpoint->next->previous = endpoint->previous;
    }
    KASSERT(!endpoint->send && !endpoint->receive);
    net_udp_discard_received(endpoint);
    flags = cpu_save_interrupts();
    net_storage_free(endpoint);
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}

enum call_status net_udp_endpoint_status(const struct udp_endpoint *endpoint)
{
  if (endpoint->local.state == UDP_STATE_SHUTDOWN) {
    return CALL_ENDPOINT_CLOSED;
  }
  if (endpoint->local.state != UDP_STATE_BOUND ||
      (!endpoint->broadcast &&
       (endpoint->address_epoch != endpoint->interface->ipv4.address_epoch ||
        endpoint->local.address != net_ipv4_address(endpoint->interface)))) {
    return CALL_UNAVAILABLE;
  }
  return CALL_OK;
}

static bool service_invalidation(void)
{
  bool worked = false;
  for (unsigned i = 0; i < UDP_INVALIDATE_BUDGET && invalidation_cursor; ++i) {
    struct udp_endpoint *endpoint = invalidation_cursor;
    invalidation_cursor = endpoint->next;
    if (endpoint->local.state == UDP_STATE_BOUND &&
        net_udp_endpoint_status(endpoint) != CALL_OK) {
      endpoint->local.state = UDP_STATE_UNAVAILABLE;
      net_udp_stop_io(endpoint, CALL_UNAVAILABLE);
    }
    worked = true;
  }
  return worked;
}

static struct udp_endpoint *find_binding(const struct net_interface *interface,
    uint32_t address, uint16_t port)
{
  for (struct udp_endpoint *endpoint = endpoints; endpoint; endpoint = endpoint->next) {
    if (net_udp_endpoint_status(endpoint) == CALL_OK && endpoint->interface == interface &&
        endpoint->local.address == address && endpoint->local.port == port) {
      return endpoint;
    }
  }
  return NULL;
}

struct udp_endpoint *net_udp_find_receiver(const struct net_interface *interface,
    uint32_t address, uint16_t port)
{
  struct udp_endpoint *endpoint = find_binding(interface, address, port);
  if (endpoint) {
    return endpoint;
  }
  if ((net_ipv4_address(interface) && address == net_ipv4_address(interface)) ||
      net_ipv4_is_broadcast(interface, address)) {
    return find_binding(interface, 0, port);
  }
  return NULL;
}

/* Wildcard and concrete bindings exclude one another only on their interface. */
static enum call_status binding_status(const struct net_interface *interface,
    uint32_t address, uint16_t port)
{
  for (struct udp_endpoint *endpoint = endpoints; endpoint; endpoint = endpoint->next) {
    if (endpoint->interface != interface || net_udp_endpoint_status(endpoint) != CALL_OK ||
        endpoint->local.port != port) {
      continue;
    }
    if (endpoint->local.address == address) {
      return CALL_ALREADY_EXISTS;
    }
    if (!address || endpoint->broadcast) {
      return CALL_BUSY;
    }
  }
  return CALL_OK;
}

static uint16_t ephemeral_port(const struct net_interface *interface, uint32_t address)
{
  for (unsigned i = UDP_EPHEMERAL_FIRST; i <= UDP_EPHEMERAL_LAST; ++i) {
    uint16_t port = next_ephemeral;
    next_ephemeral = port == UDP_EPHEMERAL_LAST ? UDP_EPHEMERAL_FIRST : port + 1;
    if (binding_status(interface, address, port) == CALL_OK) {
      return port;
    }
  }
  return 0;
}

static enum call_status open_endpoint(struct udp_control *call, const struct ipv4_route *route)
{
  bool broadcast = call->operation == CONTROL_OPEN_BROADCAST;
  const struct net_interface *interface;
  uint64_t address_epoch = 0;
  if (broadcast) {
    interface = call->pinned;
    if (!net_factory_allows(call->factory, interface)) {
      return CALL_DENIED;
    }
    if (!net_driver_mac(interface)) {
      return CALL_UNAVAILABLE;
    }
  } else {
    interface = route->interface;
    address_epoch = route->address_epoch;
  }
  if (call->port) {
    enum call_status status = binding_status(interface, call->address, call->port);
    if (status != CALL_OK) {
      return status;
    }
  }
  uint16_t port = call->port ? call->port : ephemeral_port(interface, call->address);
  if (!port) {
    return CALL_LIMIT;
  }

  uint64_t flags = cpu_save_interrupts();
  struct udp_endpoint *endpoint = net_storage_alloc(sizeof(*endpoint));
  if (!endpoint) {
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  *endpoint = (struct udp_endpoint){
    .local = {.address = call->address, .port = port, .state = UDP_STATE_BOUND,
        .interface_id = interface->id, .address_epoch = address_epoch},
    .interface = interface, .address_epoch = address_epoch, .broadcast = broadcast,
  };
  object_init(&endpoint->object, OBJECT_UDP, retire_endpoint);
  call->grant = (struct capability_grant){&endpoint->object, UDP_RIGHTS, 0, 0};
  enum capability_result result = forwarder_derive_grant(&call->grant, call->context);
  if (result != CAP_OK) {
    capability_grant_release(&call->grant);
    cpu_restore_interrupts(flags);
    return result == CAP_NO_MEMORY ? CALL_NO_MEMORY :
        result == CAP_DENIED ? CALL_DENIED : CALL_LIMIT;
  }
  KASSERT(capability_validate_grants(call->reservation.table, &call->grant, 1) == CAP_OK);
  cpu_restore_interrupts(flags);

  endpoint->next = endpoints;
  if (endpoints) {
    endpoints->previous = endpoint;
  }
  endpoints = endpoint;
  call->reply.local = endpoint->local;
  return CALL_OK;
}

static enum call_status apply_control(struct udp_control *call)
{
  switch (call->operation) {
  case CONTROL_OPEN_ROUTE: {
    struct ipv4_route route;
    enum call_status status = net_factory_route(call->factory, call->pinned,
        call->path.source, call->address, &route);
    if (status != CALL_OK) {
      return status;
    }
    /* The sole worker cannot apply configuration changes between selection
     * and publication. OPEN captures the interface and epoch of this source. */
    call->address = route.source;
    return open_endpoint(call, &route);
  }
  case CONTROL_OPEN: {
    if (!call->address) {
      return CALL_UNAVAILABLE;
    }
    struct ipv4_route route;
    enum call_status status = net_factory_bind(call->factory, call->pinned, call->address, &route);
    if (status != CALL_OK) {
      return status;
    }
    return open_endpoint(call, &route);
  }
  case CONTROL_OPEN_BROADCAST:
    return open_endpoint(call, NULL);
  case CONTROL_INSPECT:
    call->reply.local = call->endpoint->local;
    if (call->reply.local.state == UDP_STATE_BOUND &&
        net_udp_endpoint_status(call->endpoint) != CALL_OK) {
      call->reply.local.state = UDP_STATE_UNAVAILABLE;
    }
    return CALL_OK;
  case CONTROL_SHUTDOWN:
    /* Binding lookup ignores stopped objects, even while copied handles live. */
    call->endpoint->local.state = UDP_STATE_SHUTDOWN;
    net_udp_stop_io(call->endpoint, CALL_ENDPOINT_CLOSED);
    return CALL_OK;
  }
  return CALL_BAD_OPERATION;
}

static enum call_status exchange_control(struct udp_control *request,
    struct capability_table *table)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->factory) {
    enum call_status status = net_factory_path(request->factory, request->path, &request->pinned);
    if (status != CALL_OK) {
      return status;
    }
    if (request->operation == CONTROL_OPEN_BROADCAST) {
      if (request->path.source) {
        return CALL_BAD_REQUEST;
      }
      if (!request->pinned) {
        if (request->factory->interface_count != 1) {
          return CALL_BAD_REQUEST;
        }
        request->pinned = request->factory->interfaces[0];
      }
    }
  }
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }
  struct udp_control *call = net_storage_alloc(sizeof(*call));
  if (!call) {
    return CALL_NO_MEMORY;
  }
  if (table) {
    enum capability_result result = capability_request_reservation(1,
        &request->reservation, &request->slot);
    if (result != CAP_OK) {
      KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
      net_storage_free(call);
      return result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
    }
    KASSERT(request->reservation.table == table);
    if (task_stop_requested()) {
      capability_reservation_release(&request->reservation, &request->slot);
      net_storage_free(call);
      return CALL_ENDPOINT_CLOSED;
    }
  }
  lock_control();
  if (request->factory && !object_retain(&request->factory->object)) {
    unlock_control();
    capability_reservation_release(&request->reservation, &request->slot);
    net_storage_free(call);
    return CALL_LIMIT;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = *request;
  call->context = task_forwarder_operation();
  request->reservation = (struct capability_reservation){0};
  call->state = CONTROL_QUEUED;
  call->wait = wait;
  if (pending_tail) {
    pending_tail->next = call;
  } else {
    pending = call;
  }
  pending_tail = call;
  unlock_control();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_control();
  while (call->state != CONTROL_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_control();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_control();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  request->reply = call->reply;
  request->grant = call->grant;
  request->reservation = call->reservation;
  request->slot = call->slot;
  unlock_control();
  net_storage_free(call);
  if (request->factory) {
    object_release(&request->factory->object);
  }
  if (status == CALL_OK && request->grant.object) {
    capability_install_reserved(&request->reservation, &request->slot,
        &request->grant, 1, &request->reply.handle);
  }
  capability_grant_release(&request->grant);
  capability_reservation_release(&request->reservation, &request->slot);
  return status;
}

enum call_status net_udp_open(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t address,
    uint16_t port, struct udp_open_reply *reply)
{
  struct udp_control request = {
    .operation = CONTROL_OPEN, .address = address, .port = port, .factory = factory, .path = path,
  };
  enum call_status status = exchange_control(&request, table);
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  return status;
}

enum call_status net_udp_open_route(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint32_t destination,
    uint16_t port, struct udp_open_reply *reply)
{
  struct udp_control request = {
    .operation = CONTROL_OPEN_ROUTE, .address = destination, .port = port, .factory = factory, .path = path,
  };
  enum call_status status = exchange_control(&request, table);
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  return status;
}

enum call_status net_udp_open_broadcast(struct net_factory *factory,
    struct capability_table *table, struct net_path path, uint16_t port,
    struct udp_open_reply *reply)
{
  struct udp_control request = {
    .operation = CONTROL_OPEN_BROADCAST, .port = port, .factory = factory, .path = path,
  };
  enum call_status status = exchange_control(&request, table);
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  return status;
}

enum call_status net_udp_inspect(struct kernel_object *object, struct udp_endpoint_info *reply)
{
  struct udp_control request = {
    .operation = CONTROL_INSPECT, .endpoint = (struct udp_endpoint *)object,
  };
  enum call_status status = exchange_control(&request, NULL);
  if (status == CALL_OK) {
    *reply = request.reply.local;
  }
  return status;
}

enum call_status net_udp_shutdown(struct kernel_object *object)
{
  struct udp_control request = {
    .operation = CONTROL_SHUTDOWN, .endpoint = (struct udp_endpoint *)object,
  };
  return exchange_control(&request, NULL);
}

bool net_udp_service(void)
{
  assert_worker_context();
  bool worked = reap_endpoints();
  worked |= service_invalidation();
  for (unsigned i = 0; i < UDP_CONTROL_BUDGET; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_control();
    struct udp_control *call = pending;
    if (!call) {
      unlock_control();
      cpu_restore_interrupts(flags);
      break;
    }
    pending = call->next;
    if (!pending) {
      pending_tail = NULL;
    }
    call->next = NULL;
    bool cancelled = call->cancelled;
    call->state = CONTROL_RUNNING;
    unlock_control();
    cpu_restore_interrupts(flags);

    /* RUNNING gives the worker ownership of captured input and private output.
     * The caller's reserved slot stays unpublished until successful collection. */
    struct forwarder_operation *previous = task_forwarder_set(call->context);
    flags = cpu_save_interrupts();
    bool started = task_operation_start();
    cpu_restore_interrupts(flags);
    enum call_status status = !started ? CALL_DENIED :
        cancelled ? CALL_ENDPOINT_CLOSED : apply_control(call);
    task_forwarder_set(previous);

    flags = cpu_save_interrupts();
    lock_control();
    call->status = status;
    call->state = CONTROL_DONE;
    struct task_wait *wait = call->wait;
    call->wait = NULL;
    task_wait_wake(wait);
    unlock_control();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  worked |= net_udp_service_io();
  return worked;
}

void net_udp_invalidate_address(const struct net_interface *interface, uint64_t retired_epoch)
{
  assert_worker_context();
  KASSERT(interface->ipv4.address_epoch == retired_epoch);
  /* Effective epoch checks retire authority immediately; storage and pending
   * operations are drained over bounded worker turns after publication. */
  invalidation_cursor = endpoints;
  net_udp_changed();
}
