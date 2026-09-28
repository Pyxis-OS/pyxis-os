#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/object/profile.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_profile(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *profile_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_PROFILE, destroy_profile);
  }
  return object;
}

struct syscall_result profile_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  bool host = operation == PROFILE_HOST_BEGIN || operation == PROFILE_HOST_SNAPSHOT ||
      operation == PROFILE_HOST_END;
  bool file = operation == PROFILE_FILE_BEGIN || operation == PROFILE_FILE_SNAPSHOT ||
      operation == PROFILE_FILE_END;
  if (!host && !file && operation != PROFILE_BEGIN && operation != PROFILE_SNAPSHOT && operation != PROFILE_END) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & (host ? PROFILE_RIGHT_HOST : file ? PROFILE_RIGHT_FILE : PROFILE_RIGHT_MEMORY))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  union {
    struct profile_snapshot memory;
    struct profile_file_snapshot file;
    struct profile_host_snapshot host;
  } reply;
  size_t size = host ? sizeof(reply.host) : file ? sizeof(reply.file) : sizeof(reply.memory);
  bool returns_snapshot = operation != PROFILE_BEGIN && operation != PROFILE_FILE_BEGIN &&
      operation != PROFILE_HOST_BEGIN;
  if (request_size || (returns_snapshot && reply_capacity < size)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (returns_snapshot && !user_buffer_check(reply_address, size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum call_status status = host ? task_profile_host_control(operation, &reply.host) : file ? task_profile_file_control(operation, &reply.file) :
      task_profile_control(operation, &reply.memory);
  if (status != CALL_OK || !returns_snapshot) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, size));
  return (struct syscall_result){CALL_OK, size};
}
