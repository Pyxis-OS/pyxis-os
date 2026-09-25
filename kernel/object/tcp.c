#include <abi/tcp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/tcp.h>
#include <kernel/object/tcp.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

static void destroy_tcp_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *tcp_service_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_TCP_SERVICE, destroy_tcp_service);
  }
  return object;
}

struct syscall_result tcp_service_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != TCP_CONNECT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & TCP_SERVICE_RIGHT_CONNECT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct tcp_connect_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct tcp_connect_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.address, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct tcp_connect_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.reserved || !request.port) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  struct tcp_connect_reply reply;
  enum call_status status = net_tcp_connect(&process_current()->capabilities,
      request.address, request.port, request.deadline_ns, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The sole task keeps its checked mappings and table alive while parked. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result read_stream(struct kernel_object *object,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct tcp_read_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct tcp_read_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t capacity = request.capacity < TCP_READ_MAX_BYTES ? request.capacity : TCP_READ_MAX_BYTES;
  if (!user_buffer_check(request.buffer, capacity, USER_BUFFER_WRITE) ||
      !user_buffer_check(reply_address, sizeof(struct tcp_read_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  /* Both checked extents are nonoverflowing. Metadata must not overwrite data. */
  if (capacity && request.buffer < reply_address + sizeof(struct tcp_read_reply) &&
      reply_address < request.buffer + capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }

  uint8_t data[TCP_READ_MAX_BYTES];
  struct tcp_read_reply reply;
  enum call_status status = net_tcp_read(object, capacity, request.deadline_ns, data, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(request.buffer, data, reply.length));
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result write_stream(struct kernel_object *object,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct tcp_write_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size || reply_capacity < sizeof(struct tcp_write_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.buffer, request_address, payload_size) ||
      !user_buffer_check(reply_address, sizeof(struct tcp_write_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  size_t length = request.length < TCP_WRITE_MAX_BYTES ? request.length : TCP_WRITE_MAX_BYTES;
  uint8_t data[TCP_WRITE_MAX_BYTES];
  if (!copy_from_user(data, request.buffer, length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct tcp_write_reply reply;
  enum call_status status = net_tcp_write(object, data, length, request.deadline_ns, &reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* Input is already captured; an overlapping count output is harmless. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result tcp_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case TCP_INSPECT: required = TCP_RIGHT_INSPECT; break;
  case TCP_ABORT: required = TCP_RIGHT_ABORT; break;
  case TCP_READ: required = TCP_RIGHT_READ; break;
  case TCP_WRITE: required = TCP_RIGHT_WRITE; break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == TCP_READ) {
    return read_stream(object, request_address, request_size, reply_address, reply_capacity);
  }
  if (operation == TCP_WRITE) {
    return write_stream(object, request_address, request_size, reply_address, reply_capacity);
  }
  size_t reply_size = operation == TCP_INSPECT ? sizeof(struct tcp_connection_info) : 0;
  if (request_size || reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  struct tcp_connection_info reply;
  enum call_status status = operation == TCP_INSPECT ?
      net_tcp_inspect(object, &reply) : net_tcp_abort(object);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (reply_size) {
    KASSERT(copy_to_user(reply_address, &reply, reply_size));
  }
  return (struct syscall_result){CALL_OK, reply_size};
}
