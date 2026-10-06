#include <abi/directory.h>
#include <abi/mount.h>
#include <arch/smp.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/capability.h>
#include <kernel/object/disk.h>
#include <kernel/object/mount.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/random.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_disks(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *disks_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_DISKS, destroy_disks);
  }
  return object;
}

static enum call_status install_disk(struct kernel_object *object, uint64_t rights,
    handle_t *handle)
{
  enum capability_result result;
  for (;;) {
    result = capability_insert(&process_current()->capabilities, object, rights, 0, handle);
    if (result != CAP_FULL) {
      break;
    }
    result = capability_request_growth();
    if (result != CAP_OK) {
      break;
    }
  }
  return result == CAP_OK ? CALL_OK : result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
}

struct syscall_result disks_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != DISKS_ENUMERATE && operation != DISKS_OPEN) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == DISKS_ENUMERATE ? DISKS_RIGHT_ENUMERATE : DISKS_RIGHT_OPEN;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t reply_size = operation == DISKS_ENUMERATE ? sizeof(struct disk_info) :
      sizeof(struct disk_open_reply);
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uint64_t index = 0, id = 0, access = 0;
  if (operation == DISKS_ENUMERATE) {
    struct disks_enumerate_request enumerate;
    if (request_size != sizeof(enumerate)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&enumerate, request_address, sizeof(enumerate))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    index = enumerate.index;
  } else {
    struct disks_open_request open;
    if (request_size != sizeof(open)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&open, request_address, sizeof(open))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!open.id || open.id > UINT32_MAX || open.access > DISK_ACCESS_READ_WRITE) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    id = open.id;
    access = open.access;
  }
  struct npfs_request *request = npfs_request_prepare(operation == DISKS_ENUMERATE ?
      NPFS_RAW_INFO : NPFS_RAW_OPEN);
  request->job.offset = index;
  request->job.device = id;
  request->job.kind = access;
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  struct disk_info info = request->job.disk_info;
  struct kernel_object *disk = request->job.object;
  request->job.object = NULL;
  npfs_request_release(request);
  if (status == CALL_OK && operation == DISKS_ENUMERATE) {
    KASSERT(copy_to_user(reply_address, &info, sizeof(info)));
  } else if (status == CALL_OK) {
    uint64_t disk_rights = DISK_RIGHT_INFO | DISK_RIGHT_READ | DISK_RIGHT_MOUNT;
    if (access == DISK_ACCESS_READ_WRITE) {
      disk_rights |= DISK_RIGHT_WRITE | DISK_RIGHT_RELEASE;
    }
    struct disk_open_reply reply;
    status = install_disk(disk, disk_rights, &reply.disk);
    object_release(disk);
    if (status == CALL_OK) {
      KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    }
  }
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}

static struct syscall_result create_volume(struct disk_object *disk,
    uintptr_t request_address, size_t request_size)
{
  struct disk_create_volume_request create;
  if (request_size != sizeof(create)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&create, request_address, sizeof(create))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!create.partition || create.partition > UINT32_MAX ||
      !create.name_length || create.name_length > MOUNT_VOLUME_NAME_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  char name[MOUNT_VOLUME_NAME_MAX + 1];
  if (!copy_from_user(name, create.name, create.name_length)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!npfs_name_valid((const uint8_t *)name, create.name_length)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  /* The volume's identity comes from the entropy source, which needs a task. */
  uint8_t id[NPFS_ID_SIZE];
  enum call_status status = random_read(id, sizeof(id),
      task_deadline_after_ms(NPFS_TIMEOUT_MS));
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!npfs_id_valid(id)) {
    return (struct syscall_result){CALL_IO, 0};
  }
  struct npfs_request *request = npfs_request_prepare(NPFS_CREATE_VOLUME);
  request->job.device = disk->device;
  request->job.partition = (uint32_t)create.partition;
  request->job.count = create.name_length;
  memcpy(request->job.name, name, create.name_length);
  request->job.name[create.name_length] = '\0';
  memcpy(request->job.data, id, sizeof(id));
  npfs_request_submit_and_wait(request);
  status = request->job.status;
  npfs_request_release(request);
  return (struct syscall_result){status, 0};
}

struct syscall_result disk_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct disk_object *disk = (struct disk_object *)object;
  if (operation == DISK_OPEN_VOLUME) {
    if (!(rights & DISK_RIGHT_MOUNT)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return mount_open_device(disk->device, rights & DISK_RIGHT_WRITE,
        request_address, request_size, reply_address, reply_capacity);
  }
  if (operation == DISK_CREATE_VOLUME) {
    if ((rights & (DISK_RIGHT_MOUNT | DISK_RIGHT_WRITE)) !=
        (DISK_RIGHT_MOUNT | DISK_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return create_volume(disk, request_address, request_size);
  }
  uint64_t required;
  enum npfs_operation worker_operation;
  size_t reply_size = 0;
  uint64_t offset = 0, length = 0, source = 0;
  switch (operation) {
  case DISK_INFO:
    required = DISK_RIGHT_INFO;
    worker_operation = NPFS_RAW_INFO;
    reply_size = sizeof(struct disk_info);
    break;
  case DISK_READ:
    required = DISK_RIGHT_READ;
    worker_operation = NPFS_RAW_READ;
    break;
  case DISK_WRITE:
    required = DISK_RIGHT_WRITE;
    worker_operation = NPFS_RAW_WRITE;
    break;
  case DISK_FLUSH:
    required = DISK_RIGHT_WRITE;
    worker_operation = NPFS_RAW_FLUSH;
    break;
  case DISK_RELEASE:
    required = DISK_RIGHT_RELEASE;
    worker_operation = NPFS_RAW_RELEASE;
    break;
  case DISK_CLAIM:
    required = DISK_RIGHT_WRITE;
    worker_operation = NPFS_RAW_CLAIM;
    break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == DISK_READ) {
    struct disk_read_request read;
    if (request_size != sizeof(read)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&read, request_address, sizeof(read))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    offset = read.offset;
    length = read.length;
    reply_size = length;
  } else if (operation == DISK_WRITE) {
    struct disk_write_request write;
    if (request_size != sizeof(write)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&write, request_address, sizeof(write))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    offset = write.offset;
    length = write.length;
    source = write.data;
  } else if (operation == DISK_CLAIM) {
    struct disk_claim_request claim;
    if (request_size != sizeof(claim)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&claim, request_address, sizeof(claim))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (claim.partition > UINT32_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    offset = claim.partition;
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == DISK_READ || operation == DISK_WRITE) {
    if (!length || length > DISK_IO_MAX_BYTES || length > UINT64_MAX - offset) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == DISK_WRITE && !user_buffer_check(source, length, USER_BUFFER_READ)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct npfs_request *request = npfs_request_prepare(worker_operation);
  request->job.raw = disk;
  request->job.device = disk->device;
  request->job.offset = offset;
  request->job.count = length;
  if (operation == DISK_WRITE) {
    KASSERT(copy_from_user(request->job.data, source, length));
  }
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  if (status == CALL_OK && reply_size) {
    const void *reply = operation == DISK_INFO ? (const void *)&request->job.disk_info :
        (const void *)request->job.data;
    KASSERT(copy_to_user(reply_address, reply, reply_size));
  }
  npfs_request_release(request);
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}
