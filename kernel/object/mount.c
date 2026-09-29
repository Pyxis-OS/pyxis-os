#include <abi/mount.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/mm/heap.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_mount(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *mount_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_MOUNT, destroy_mount);
  }
  return object;
}

struct syscall_result mount_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (operation != MOUNT_OPEN_ROOT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & MOUNT_RIGHT_OPEN_ROOT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct mount_open_request open;
  struct mount_reply reply;
  if (request_size != sizeof(open) || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&open, request_address, sizeof(open)) ||
      !user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  if (open.access != MOUNT_ACCESS_READ_ONLY && open.access != MOUNT_ACCESS_READ_WRITE) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  uint64_t directory_rights = open.access == MOUNT_ACCESS_READ_WRITE ? DIRECTORY_RIGHTS :
      DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES;

  struct hostfs_request *request = task_prepare_hostfs(HOSTFS_ROOT);
  task_submit_hostfs(request);
  if (request->status != CALL_OK) {
    return (struct syscall_result){request->status, 0};
  }

  /* Keep the returned reference across capability-table growth. The mount
   * authority and the resulting directory have independent lifetimes. */
  struct kernel_object *root = request->object;
  enum capability_result result;
  for (;;) {
    result = capability_insert(&process_current()->capabilities, root,
        directory_rights, 0, &reply.root);
    if (result != CAP_FULL) {
      break;
    }
    result = capability_request_growth();
    if (result != CAP_OK) {
      break;
    }
  }
  object_release(root);
  if (result != CAP_OK) {
    KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
    return (struct syscall_result){result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
