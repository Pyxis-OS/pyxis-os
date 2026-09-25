#include <abi/random.h>
#include <kernel/mm/heap.h>
#include <kernel/object/random.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>
#include <kernel/virtio/rng.h>

static void destroy_random(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *random_create(void)
{
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_RANDOM, destroy_random);
  }
  return object;
}

struct syscall_result random_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != RANDOM_READ) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & RANDOM_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct random_read_request request = {0};
  size_t payload_size = sizeof(request) - sizeof(request.header);
  if (request_size != payload_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.length, request_address, payload_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (request.length > RANDOM_MAX_BYTES) {
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  if (reply_capacity < request.length) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, request.length, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uint8_t bytes[RANDOM_MAX_BYTES];
  enum call_status status = virtio_rng_read(bytes, request.length, request.deadline_ns);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  /* The live grant and the sole task's checked output mapping survived sleep. */
  KASSERT(copy_to_user(reply_address, bytes, request.length));
  return (struct syscall_result){CALL_OK, request.length};
}
