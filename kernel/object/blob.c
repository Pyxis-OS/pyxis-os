#include <abi/blob.h>
#include <arch/smp.h>
#include <kernel/object/blob.h>
#include <kernel/initrd.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

static void destroy_blob(struct kernel_object *object)
{
  kfree((struct blob_object *)object);
}

struct blob_object *blob_create(const struct initrd_file *file)
{
  KASSERT(arch_cpu_index() == 0 && file && file->data);
  struct blob_object *blob = kmalloc(sizeof(*blob));
  if (!blob) {
    return NULL;
  }
  object_init(&blob->object, OBJECT_BLOB, destroy_blob);
  blob->data = file->data;
  blob->size = file->size;
  return blob;
}

static struct syscall_result read_blob(struct blob_object *blob,
    const struct blob_read_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct blob_read_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request->address, request->capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  size_t count = 0;
  if (request->offset < blob->size) {
    count = blob->size - request->offset;
    if (count > request->capacity) {
      count = request->capacity;
    }
  }
  /* Compare before subtracting; never form data + offset for EOF or an empty
   * transfer. The immutable archive cannot alias writable user backing. */
  if (count) {
    KASSERT(copy_to_user(request->address,
        (const uint8_t *)blob->data + request->offset, count));
  }

  reply.read = count;
  /* IF=0 and private stable mappings keep both checked destinations writable.
   * The captured request permits overlap; the reply wins over copied data. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result blob_call(struct blob_object *blob, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != BLOB_READ && operation != BLOB_SIZE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & BLOB_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  struct blob_read_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == BLOB_READ) {
    return read_blob(blob, &request, reply_address, reply_capacity);
  }

  struct blob_size_reply reply = {.size = blob->size};
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
