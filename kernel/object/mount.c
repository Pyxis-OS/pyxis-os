#include <abi/mount.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user_memory.h>

enum mount_backend { MOUNT_HOST, MOUNT_NATIVE };

struct mount_object {
  struct kernel_object object;
  enum mount_backend backend;
  struct mount_config config;
  enum call_status setup_status;
};

static void destroy_mount(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *mount_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct mount_object *mount = kmalloc(sizeof(*mount));
  if (!mount) {
    return NULL;
  }
  *mount = (struct mount_object){.backend = MOUNT_HOST};
  object_init(&mount->object, OBJECT_MOUNT, destroy_mount);
  return &mount->object;
}

struct kernel_object *mount_create_native(const struct mount_config *config)
{
  KASSERT(arch_cpu_index() == 0 && config->enabled);
  enum block_preparation preparation = block_preparation_result();
  KASSERT(preparation != BLOCK_DEVICE_ABSENT);
  struct mount_object *mount = kmalloc(sizeof(*mount));
  if (!mount) {
    return NULL;
  }
  *mount = (struct mount_object){
    .backend = MOUNT_NATIVE,
    .config = *config,
    .setup_status = preparation == BLOCK_DEVICE_READY ? CALL_OK :
        preparation == BLOCK_DEVICE_AMBIGUOUS ? CALL_IO : CALL_UNAVAILABLE,
  };
  object_init(&mount->object, OBJECT_MOUNT, destroy_mount);
  if (mount->setup_status != CALL_OK) {
    klog("mount: configured native authority has block preparation failure %u\n",
         (unsigned)preparation);
  }
  return &mount->object;
}

static enum call_status open_native(struct mount_object *mount, uint64_t rights,
    uintptr_t request_address, size_t request_size, struct kernel_object **root,
    uint64_t *directory_rights)
{
  struct mount_volume_request open;
  if (request_size != sizeof(open)) {
    return CALL_BAD_REQUEST;
  }
  if (!copy_from_user(&open, request_address, sizeof(open))) {
    return CALL_BAD_BUFFER;
  }
  if (!open.partition || open.partition > UINT32_MAX ||
      !open.name_length || open.name_length > MOUNT_VOLUME_NAME_MAX ||
      (open.rights & ~DIRECTORY_RIGHTS) || !(open.rights & DIRECTORY_RIGHT_LOOKUP)) {
    return CALL_BAD_REQUEST;
  }
  if ((open.rights & (DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES |
      DIRECTORY_RIGHT_REMOVE)) && !(rights & MOUNT_RIGHT_WRITE)) {
    return CALL_DENIED;
  }
  if ((open.rights & DIRECTORY_RIGHT_FILESYSTEM_INFO) && !(rights & MOUNT_RIGHT_OBSERVE)) {
    return CALL_DENIED;
  }
  char name[MOUNT_VOLUME_NAME_MAX + 1];
  if (!copy_from_user(name, open.name, open.name_length)) {
    return CALL_BAD_BUFFER;
  }
  if (!npfs_name_valid((const uint8_t *)name, open.name_length)) {
    return CALL_BAD_REQUEST;
  }
  name[open.name_length] = '\0';
  if (mount->setup_status != CALL_OK) {
    return mount->setup_status;
  }

  struct npfs_request *request = npfs_request_prepare(NPFS_ROOT);
  request->job.disk = mount->config.disk;
  request->job.partition = open.partition;
  request->job.rights = open.rights;
  request->job.count = open.name_length;
  memcpy(request->job.name, name, open.name_length + 1);
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  *root = request->job.object;
  request->job.object = NULL;
  npfs_request_release(request);
  *directory_rights = open.rights;
  return status;
}

static enum call_status open_host(uintptr_t request_address, size_t request_size,
    struct kernel_object **root, uint64_t *directory_rights)
{
  struct mount_open_request open;
  if (request_size != sizeof(open)) {
    return CALL_BAD_REQUEST;
  }
  if (!copy_from_user(&open, request_address, sizeof(open))) {
    return CALL_BAD_BUFFER;
  }
  if (open.access != MOUNT_ACCESS_READ_ONLY && open.access != MOUNT_ACCESS_READ_WRITE) {
    return CALL_BAD_REQUEST;
  }
  *directory_rights = open.access == MOUNT_ACCESS_READ_WRITE ? DIRECTORY_CONTENT_RIGHTS :
      DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES;

  struct hostfs_request *request = hostfs_request_prepare(HOSTFS_ROOT);
  hostfs_request_submit_and_wait(request);
  enum call_status status = request->status;
  *root = request->object;
  request->object = NULL;
  hostfs_request_release(request);
  return status;
}

struct syscall_result mount_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct mount_object *mount = (struct mount_object *)object;
  if (operation == MOUNT_SYNC && mount->backend == MOUNT_NATIVE) {
    if (!(rights & MOUNT_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (mount->setup_status != CALL_OK) {
      return (struct syscall_result){mount->setup_status, 0};
    }
    struct npfs_request *request = npfs_request_prepare(NPFS_DISK_SYNC);
    request->job.disk = mount->config.disk;
    npfs_request_submit_and_wait(request);
    enum call_status status = request->job.status;
    npfs_request_release(request);
    return (struct syscall_result){status, 0};
  }
  if (operation != (mount->backend == MOUNT_HOST ? MOUNT_OPEN_ROOT : MOUNT_OPEN_VOLUME)) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & MOUNT_RIGHT_OPEN_ROOT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct mount_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct kernel_object *root = NULL;
  uint64_t directory_rights = 0;
  enum call_status status = mount->backend == MOUNT_NATIVE ?
      open_native(mount, rights, request_address, request_size, &root, &directory_rights) :
      open_host(request_address, request_size, &root, &directory_rights);
  if (status != CALL_OK) {
    KASSERT(!root);
    return (struct syscall_result){status, 0};
  }
  KASSERT(root);

  /* Keep the returned reference across capability-table growth. The mount
   * authority and the resulting directory have independent lifetimes. */
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
