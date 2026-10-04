#include <abi/udp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/udp.h>
#include <kernel/object/udp.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_udp_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *udp_service_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_UDP_SERVICE, destroy_udp_service);
  }
  return object;
}

struct syscall_result udp_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case UDP_OPEN:
  case UDP_OPEN_ROUTE: required = UDP_SERVICE_RIGHT_OPEN; break;
  case UDP_OPEN_BROADCAST: required = UDP_SERVICE_RIGHT_BROADCAST; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct udp_open_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct udp_open_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.address, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct udp_open_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved || (operation == UDP_OPEN_BROADCAST && request.address)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct udp_open_reply reply;
  struct capability_table *table = &process_current()->capabilities;
  enum call_status status;
  switch (operation) {
  case UDP_OPEN_ROUTE:
    status = net_udp_open_route(table, request.address, request.port, &reply);
    break;
  case UDP_OPEN_BROADCAST:
    status = net_udp_open_broadcast(table, request.port, &reply);
    break;
  default:
    status = net_udp_open(table, request.address, request.port, &reply);
    break;
  }
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The sole task's checked mappings remain valid across the table loan. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result send_datagram(struct kernel_object *object,
    uintptr_t request_address, size_t request_size)
{
  struct udp_send_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.address, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved || !request.port) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (request.length > UDP_MAX_PAYLOAD) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  uint8_t data[UDP_MAX_PAYLOAD];
  if (!copy_from_user(data, request.buffer, request.length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = net_udp_send(object, request.address, request.port,
      data, request.length, request.deadline_ns);
  return (struct syscall_result){status, 0};
}

static struct syscall_result receive_datagram(struct kernel_object *object,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct udp_receive_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct udp_receive_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t capacity = request.capacity < UDP_MAX_PAYLOAD ? request.capacity : UDP_MAX_PAYLOAD;
  if (!user_buffer_check(request.buffer, capacity, USER_BUFFER_WRITE) ||
      !user_buffer_check(reply_address, sizeof(struct udp_receive_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  /* Checked ranges cannot overflow. Overlapping outputs could otherwise make
   * successful metadata overwrite the payload just returned to the caller. */
  if (capacity && request.buffer < reply_address + sizeof(struct udp_receive_reply) &&
      reply_address < request.buffer + capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint8_t data[UDP_MAX_PAYLOAD];
  struct udp_receive_reply reply;
  enum call_status status = net_udp_receive(object, capacity, request.deadline_ns, data, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(request.buffer, data, reply.length));
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result udp_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case UDP_INSPECT: required = UDP_RIGHT_INSPECT; break;
  case UDP_SHUTDOWN: required = UDP_RIGHT_SHUTDOWN; break;
  case UDP_SEND: required = UDP_RIGHT_SEND; break;
  case UDP_RECEIVE: required = UDP_RIGHT_RECEIVE; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == UDP_SEND) {
    return send_datagram(object, request_address, request_size);
  }
  if (operation == UDP_RECEIVE) {
    return receive_datagram(object, request_address, request_size, reply_address, reply_capacity);
  }
  size_t reply_size = operation == UDP_INSPECT ? sizeof(struct udp_endpoint_info) : 0;
  if (request_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct udp_endpoint_info reply;
  enum call_status status = operation == UDP_INSPECT ?
      net_udp_inspect(object, &reply) : net_udp_shutdown(object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply_size) {
    KASSERT(copy_to_user(reply_address, &reply, reply_size));
  }
  return (struct syscall_result){CALL_OK, reply_size};
}
