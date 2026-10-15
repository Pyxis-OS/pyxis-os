#include <kernel/object/forwarder.h>
#include <abi/directory.h>
#include <abi/mount.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/fs/npfs.h>
#include <kernel/fs/fat32.h>
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
    struct capability_reservation *reservation, const struct capability_reserved_slot *slot,
    handle_t *handle)
{
  struct capability_grant grant = {0};
  enum capability_result result = capability_grant_retain(object, rights, 0, &grant);
  if (result == CAP_OK) {
    result = forwarder_request_derivation(&grant);
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(reservation->table, &grant, 1);
  }
  if (result == CAP_OK) {
    capability_install_reserved(reservation, slot, &grant, 1, handle);
  }
  capability_grant_release(&grant);
  return result == CAP_OK ? CALL_OK : result == CAP_NO_MEMORY ? CALL_NO_MEMORY : result == CAP_DENIED ? CALL_DENIED : CALL_LIMIT;
}

static bool selection_valid(const struct disk_partition_selection *selection)
{
  return selection->id && selection->id <= UINT32_MAX && selection->partition &&
      selection->partition <= UINT32_MAX && selection->device_block_size &&
      selection->device_block_count &&
      selection->device_block_count <= UINT64_MAX / selection->device_block_size &&
      selection->first_block && selection->first_block < selection->device_block_count &&
      selection->block_count &&
      selection->block_count <= selection->device_block_count - selection->first_block;
}

static struct syscall_result selected_open(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  bool esp = operation == DISKS_REVIEW_ESP;
  bool review = esp || operation == DISKS_REVIEW_ERASE || operation == DISKS_REVIEW_APPEND;
  bool whole_review = review && !esp;
  uint64_t required = operation == DISKS_REVIEW_ERASE ? DISKS_RIGHT_ERASE :
      operation == DISKS_REVIEW_APPEND ? DISKS_RIGHT_APPEND_POOL :
      esp ? DISKS_RIGHT_REBUILD_ESP :
      operation == DISKS_OPEN_POOL ? DISKS_RIGHT_OPEN_POOL : DISKS_RIGHT_OPEN_PARTITION;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct disks_review_request reviewed = {0};
  struct disk_partition_selection selection = {0};
  size_t expected = whole_review ? sizeof(reviewed) : sizeof(selection);
  if (request_size != expected || reply_capacity < sizeof(struct disk_open_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(whole_review ? (void *)&reviewed : (void *)&selection, request_address, expected) ||
      !user_buffer_check(reply_address, sizeof(struct disk_open_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (whole_review ? (!reviewed.id || reviewed.id > UINT32_MAX || !reviewed.block_size ||
      !reviewed.block_count || reviewed.block_count > UINT64_MAX / reviewed.block_size) :
      !selection_valid(&selection)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return (struct syscall_result){reserved == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct npfs_request *request = npfs_request_prepare(review ? NPFS_RAW_REVIEW :
      NPFS_RAW_OPEN_PARTITION);
  request->job.device = whole_review ? reviewed.id : selection.id;
  request->job.kind = operation == DISKS_REVIEW_ERASE ? DISK_ERASE_REVIEW :
      operation == DISKS_REVIEW_APPEND ? DISK_APPEND_REVIEW :
      esp ? DISK_ESP_REVIEW :
      operation == DISKS_OPEN_POOL ? DISK_POOL : DISK_RAW_PARTITION;
  request->job.rights = review ? DISK_RIGHT_INFO | DISK_RIGHT_READ |
      (operation == DISKS_REVIEW_ERASE ? DISK_RIGHT_BEGIN_ERASE :
       esp ? DISK_RIGHT_REBUILD_ESP : DISK_RIGHT_APPEND_POOL) :
      DISK_RIGHTS;
  request->job.disk_review = reviewed;
  request->job.disk_selection = selection;
  request->job.reservation = reservation;
  request->job.slot = slot;
  reservation = (struct capability_reservation){0};
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  struct disk_open_reply reply = {.disk = request->job.handle};
  npfs_request_release(request);
  if (status == CALL_OK && !copy_returned_handles_to_user(reply_address,
      &reply, sizeof(reply), &reply.disk, 1)) {
    status = CALL_BAD_BUFFER;
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

static struct syscall_result consume_review(struct disk_object *disk, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  bool append = operation == DISK_APPEND_POOL;
  bool esp = operation == DISK_BEGIN_ESP_REBUILD;
  uint64_t required = append ? DISK_RIGHT_APPEND_POOL :
      esp ? DISK_RIGHT_REBUILD_ESP : DISK_RIGHT_BEGIN_ERASE;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  size_t reply_size = append ? sizeof(struct disk_append_reply) : sizeof(struct disk_open_reply);
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct disk_partition_selection selection = {0};
  if (append) {
    if (request_size != sizeof(selection)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&selection, request_address, sizeof(selection))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!selection_valid(&selection)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  } else {
    struct disk_begin_erase_request consent;
    if (request_size != sizeof(consent)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&consent, request_address, sizeof(consent))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    const char *confirmation = esp ? DISK_ESP_REBUILD_CONFIRMATION : DISK_ERASE_CONFIRMATION;
    size_t confirmation_length = esp ? sizeof(DISK_ESP_REBUILD_CONFIRMATION) - 1 :
        sizeof(DISK_ERASE_CONFIRMATION) - 1;
    if (consent.confirmation_length != confirmation_length) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    char word[sizeof(DISK_ESP_REBUILD_CONFIRMATION) - 1];
    if (!copy_from_user(word, consent.confirmation, confirmation_length)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (memcmp(word, confirmation, confirmation_length)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
  if (reserved != CAP_OK) {
    return (struct syscall_result){reserved == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct npfs_request *request = npfs_request_prepare(append ? NPFS_RAW_APPEND_POOL : NPFS_RAW_BEGIN_ERASE);
  request->job.raw = disk;
  request->job.device = disk->device;
  request->job.disk_selection = selection;
  request->job.disk_append = (struct disk_append_reply){
    .partition = selection, .failed_block = UINT64_MAX,
  };
  request->job.rights = append ? DISK_RIGHTS :
      DISK_RIGHT_INFO | DISK_RIGHT_READ | DISK_RIGHT_WRITE | DISK_RIGHT_RELEASE;
  request->job.reservation = reservation;
  request->job.slot = slot;
  reservation = (struct capability_reservation){0};
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  struct disk_append_reply progress = request->job.disk_append;
  progress.disk = request->job.handle;
  struct disk_open_reply opened = {.disk = request->job.handle};
  npfs_request_release(request);
  if (status == CALL_OK) {
    if (!copy_returned_handles_to_user(reply_address, append ? (void *)&progress : (void *)&opened,
        reply_size, &opened.disk, 1)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else if (append && !copy_to_user(reply_address, &progress, sizeof(progress))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){status, status == CALL_OK || append ? reply_size : 0};
}

struct syscall_result disks_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation >= DISKS_REVIEW_ERASE && operation <= DISKS_REVIEW_ESP) {
    return selected_open(rights, operation, request_address, request_size, reply_address, reply_capacity);
  }
  if (operation == DISKS_OPEN_FAT32) {
    return fat32_open_selected(rights, request_address, request_size,
        reply_address, reply_capacity);
  }
  if (operation != DISKS_ENUMERATE && operation != DISKS_OPEN) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  uint64_t required = operation == DISKS_ENUMERATE ? DISKS_RIGHT_ENUMERATE : DISKS_RIGHT_OPEN;
  if (!(rights & (required | (operation == DISKS_OPEN ? DISKS_RIGHT_OPEN_READ_ONLY : 0)))) {
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
    if (access == DISK_ACCESS_READ_WRITE && !(rights & DISKS_RIGHT_OPEN)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (operation == DISKS_OPEN) {
    enum capability_result reserved = capability_request_reservation(1, &reservation, &slot);
    if (reserved != CAP_OK) {
      return (struct syscall_result){reserved == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT, 0};
    }
    if (task_stop_requested()) {
      capability_reservation_release(&reservation, &slot);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
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
    if (!copy_to_user(reply_address, &info, sizeof(info))) {
      status = CALL_BAD_BUFFER;
    }
  } else if (status == CALL_OK) {
    uint64_t disk_rights = DISK_RIGHT_INFO | DISK_RIGHT_READ | DISK_RIGHT_MOUNT;
    if (access == DISK_ACCESS_READ_WRITE) {
      disk_rights |= DISK_RIGHT_WRITE | DISK_RIGHT_RELEASE;
    }
    struct disk_open_reply reply;
    status = task_stop_requested() ? CALL_ENDPOINT_CLOSED :
        install_disk(disk, disk_rights, &reservation, &slot, &reply.disk);
    if (status == CALL_OK) {
      if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.disk, 1)) {
        status = CALL_BAD_BUFFER;
      }
    }
  }
  capability_reservation_release(&reservation, &slot);
  if (disk) {
    object_release(disk);
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
      arch_monotonic_ns() + RANDOM_MAX_WAIT_NS);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!npfs_id_valid(id)) {
    return (struct syscall_result){CALL_IO, 0};
  }
  struct npfs_request *request = npfs_request_prepare(NPFS_CREATE_VOLUME);
  request->job.device = disk->device;
  request->job.raw = disk;
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
  if (operation == DISK_BEGIN_ERASE || operation == DISK_APPEND_POOL ||
      operation == DISK_BEGIN_ESP_REBUILD) {
    return consume_review(disk, rights, operation, request_address, request_size,
        reply_address, reply_capacity);
  }
  if (operation == DISK_OPEN_FAT32) {
    if (!(rights & DISK_RIGHT_MOUNT)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (disk->kind == DISK_POOL) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return fat32_open_device(disk, rights, request_address, request_size,
        reply_address, reply_capacity);
  }
  if (operation == DISK_OPEN_VOLUME) {
    if (!(rights & DISK_RIGHT_MOUNT)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    return mount_open_device(disk, rights & DISK_RIGHT_WRITE,
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
  case DISK_PARTITION_INFO:
    required = DISK_RIGHT_INFO;
    worker_operation = NPFS_RAW_PARTITION_INFO;
    reply_size = sizeof(struct disk_partition_selection);
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
  } else if (operation == DISK_CLAIM || operation == DISK_PARTITION_INFO) {
    struct disk_claim_request claim;
    if (request_size != sizeof(claim)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&claim, request_address, sizeof(claim))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (claim.partition > UINT32_MAX || (operation == DISK_PARTITION_INFO && !claim.partition)) {
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
    if (!copy_from_user(request->job.data, source, length)) {
      request->job.raw = NULL;
      bsp_request_cancel_prepared(&request->request);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  }
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  if (status == CALL_OK && reply_size) {
    const void *reply = operation == DISK_INFO ? (const void *)&request->job.disk_info :
        operation == DISK_PARTITION_INFO ? (const void *)&request->job.disk_selection :
        (const void *)request->job.data;
    if (!copy_to_user(reply_address, reply, reply_size)) {
      status = CALL_BAD_BUFFER;
    }
  }
  npfs_request_release(request);
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}
