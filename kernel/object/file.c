#include <abi/file.h>
#include <arch/smp.h>
#include <kernel/object/file.h>
#include <kernel/initrd.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/user_memory.h>

static void destroy_file(struct kernel_object *object)
{
  kfree((struct file_object *)object);
}

struct file_object *file_create_initrd(const struct initrd_file *view)
{
  KASSERT(arch_cpu_index() == 0 && view && view->data);
  struct file_object *file = kmalloc(sizeof(*file));
  if (!file) {
    return NULL;
  }
  object_init(&file->object, OBJECT_FILE, destroy_file);
  file->backing = FILE_INITRD;
  file->data = view->data;
  file->size = view->size;
  return file;
}

struct file_object *file_create_ram(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct file_object *file = kmalloc(sizeof(*file));
  if (!file) {
    return NULL;
  }
  *file = (struct file_object){.backing = FILE_RAM};
  object_init(&file->object, OBJECT_FILE, destroy_file);
  return file;
}

static struct syscall_result read_file(struct file_object *file,
    const struct file_read_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct file_read_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request->address, request->capacity, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  size_t count = 0;
  if (request->offset < file->size) {
    count = file->size - request->offset;
    if (count > request->capacity) {
      count = request->capacity;
    }
  }
  /* Compare before subtracting; never form data + offset for EOF or an empty
   * transfer. The immutable archive cannot alias writable user backing. */
  if (count) {
    KASSERT(copy_to_user(request->address,
        (const uint8_t *)file->data + request->offset, count));
  }

  reply.read = count;
  /* IF=0 and private stable mappings keep both checked destinations writable.
   * The captured request permits overlap; the reply wins over copied data. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation != FILE_READ && operation != FILE_SIZE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & FILE_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  struct file_read_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == FILE_READ) {
    return read_file(file, &request, reply_address, reply_capacity);
  }

  struct file_size_reply reply = {.size = file->size};
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
