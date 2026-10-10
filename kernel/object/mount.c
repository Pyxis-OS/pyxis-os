#include <abi/mount.h>
#include <arch/smp.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

enum mount_backend { MOUNT_HOST, MOUNT_NATIVE };

struct mount_object {
  struct kernel_object object;
  enum mount_backend backend;
  struct mount_config config;
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
  struct mount_object *mount = kmalloc(sizeof(*mount));
  if (!mount) {
    return NULL;
  }
  *mount = (struct mount_object){
    .backend = MOUNT_NATIVE,
    .config = *config,
  };
  object_init(&mount->object, OBJECT_MOUNT, destroy_mount);
  return &mount->object;
}

static enum call_status reserve_root(struct capability_reservation *reservation,
    struct capability_reserved_slot *slot)
{
  enum capability_result result = capability_request_reservation(1, reservation, slot);
  if (result != CAP_OK) {
    KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
    return result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }
  if (task_stop_requested()) {
    capability_reservation_release(reservation, slot);
    return CALL_ENDPOINT_CLOSED;
  }
  return CALL_OK;
}

static enum call_status open_native(const struct gpt_guid *disk, block_device_id device,
    uint64_t rights,
    uintptr_t request_address, size_t request_size, struct kernel_object **root,
    uint64_t *directory_rights, struct capability_reservation *reservation,
    struct capability_reserved_slot *slot)
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
  enum call_status status = reserve_root(reservation, slot);
  if (status != CALL_OK) {
    return status;
  }
  /* The claim stays on this caller; the worker receives no stack pointer. */
  struct npfs_request *request = npfs_request_prepare(NPFS_ROOT);
  if (disk) {
    request->job.disk = *disk;
  }
  request->job.device = device;
  request->job.partition = open.partition;
  request->job.rights = open.rights;
  request->job.count = open.name_length;
  memcpy(request->job.name, name, open.name_length + 1);
  npfs_request_submit_and_wait(request);
  status = request->job.status;
  *root = request->job.object;
  request->job.object = NULL;
  npfs_request_release(request);
  *directory_rights = open.rights;
  return status;
}

static enum call_status open_host(uintptr_t request_address, size_t request_size,
    struct kernel_object **root, uint64_t *directory_rights,
    struct capability_reservation *reservation, struct capability_reserved_slot *slot)
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

  enum call_status status = reserve_root(reservation, slot);
  if (status != CALL_OK) {
    return status;
  }
  struct hostfs_request *request = hostfs_request_prepare(HOSTFS_ROOT);
  hostfs_request_submit_and_wait(request);
  status = request->status;
  *root = request->object;
  request->object = NULL;
  hostfs_request_release(request);
  return status;
}

static struct syscall_result install_root(struct kernel_object *root,
    uint64_t directory_rights, uintptr_t reply_address,
    struct capability_reservation *reservation, const struct capability_reserved_slot *slot)
{
  struct mount_reply reply;
  struct capability_grant grant = {0};
  enum call_status status = CALL_ENDPOINT_CLOSED;
  if (!task_stop_requested()) {
    enum capability_result result = capability_grant_retain(root, directory_rights, 0, &grant);
    if (result == CAP_OK) {
      result = capability_validate_grants(reservation->table, &grant, 1);
    }
    if (result == CAP_OK) {
      capability_install_reserved(reservation, slot, &grant, 1, &reply.root);
      status = CALL_OK;
    } else {
      KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
      status = result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
    }
  }
  capability_grant_release(&grant);
  capability_reservation_release(reservation, slot);
  object_release(root);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
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
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum call_status status = mount->backend == MOUNT_NATIVE ?
      open_native(&mount->config.disk, BLOCK_DEVICE_ID_NONE, rights,
          request_address, request_size, &root, &directory_rights, &reservation, &slot) :
      open_host(request_address, request_size, &root, &directory_rights, &reservation, &slot);
  if (status != CALL_OK) {
    KASSERT(!root);
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){status, 0};
  }
  KASSERT(root);

  return install_root(root, directory_rights, reply_address, &reservation, &slot);
}

struct syscall_result mount_open_device(block_device_id device, bool writable,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (reply_capacity < sizeof(struct mount_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct mount_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct kernel_object *root = NULL;
  uint64_t directory_rights = 0;
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  uint64_t rights = MOUNT_RIGHT_OPEN_ROOT | MOUNT_RIGHT_OBSERVE |
      (writable ? MOUNT_RIGHT_WRITE : 0);
  enum call_status status = open_native(NULL, device, rights, request_address, request_size,
      &root, &directory_rights, &reservation, &slot);
  if (status != CALL_OK) {
    KASSERT(!root);
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){status, 0};
  }
  return install_root(root, directory_rights, reply_address, &reservation, &slot);
}
