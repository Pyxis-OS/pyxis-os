#include <abi/file.h>
#include <arch/smp.h>
#include <kernel/object/file.h>
#include <kernel/initrd.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_file(struct kernel_object *object)
{
  struct file_object *file = (struct file_object *)object;
  KASSERT(!file->busy && !file->first_waiter);
  if (file->backing == FILE_RAM) {
    kfree((void *)file->data);
  }
  kfree(file);
}

static struct file_object *create_file(enum file_backing backing)
{
  KASSERT(arch_cpu_index() == 0);
  struct file_object *file = kmalloc(sizeof(*file));
  if (!file) {
    return NULL;
  }
  *file = (struct file_object){.backing = backing};
  atomic_init(&file->locked, false);
  object_init(&file->object, OBJECT_FILE, destroy_file);
  return file;
}

struct file_object *file_create_initrd(const struct initrd_file *view)
{
  KASSERT(view && view->data);
  struct file_object *file = create_file(FILE_INITRD);
  if (file) {
    file->data = view->data;
    file->size = view->size;
  }
  return file;
}

struct file_object *file_create_ram(void)
{
  return create_file(FILE_RAM);
}

/* IF=0. Lock order is file -> scheduler queues. No allocation or sleep while
 * held; busy reserves the operation while its owner sleeps for BSP service. */
static void lock_file(struct file_object *file)
{
  while (atomic_exchange_explicit(&file->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_file(struct file_object *file)
{
  atomic_store_explicit(&file->locked, false, memory_order_release);
}

static void begin_operation(struct file_object *file)
{
  lock_file(file);
  if (!file->busy) {
    file->busy = true;
    unlock_file(file);
    return;
  }

  struct file_wait *waiter = task_prepare_file_wait();
  struct task_wait *wait = waiter->wait;
  if (file->last_waiter) {
    file->last_waiter->next = waiter;
  } else {
    file->first_waiter = waiter;
  }
  file->last_waiter = waiter;
  unlock_file(file);
  task_wait_sleep(wait);
  /* The previous owner handed busy directly to us before waking. Our handle
   * retains the file throughout the wait, including while lending it to BSP. */
}

static void end_operation(struct file_object *file)
{
  lock_file(file);
  struct file_wait *waiter = file->first_waiter;
  if (waiter) {
    file->first_waiter = waiter->next;
    if (!file->first_waiter) {
      file->last_waiter = NULL;
    }
    struct task_wait *wait = waiter->wait;
    waiter->next = NULL;
    waiter->wait = NULL;
    task_wait_wake(wait);
    /* Never touch waiter after wake; its task may run and retire immediately. */
  } else {
    file->busy = false;
  }
  unlock_file(file);
}

bool file_replace_buffer(struct file_object *file, size_t capacity)
{
  KASSERT(arch_cpu_index() == 0 && file->backing == FILE_RAM && file->busy);
  KASSERT(!capacity || capacity >= file->size);
  void *data = NULL;
  if (capacity) {
    data = kmalloc(capacity);
    if (!data) {
      return false;
    }
    if (file->size) {
      memcpy(data, file->data, file->size);
    }
  }
  kfree((void *)file->data);
  file->data = data;
  file->capacity = capacity;
  return true;
}

static bool reserve_buffer(struct file_object *file, size_t size)
{
  if (size <= file->capacity) {
    return true;
  }
  size_t capacity = size;
  if (file->capacity <= SIZE_MAX / 2 && capacity < file->capacity * 2) {
    capacity = file->capacity * 2;
  }
  if (task_replace_file_buffer(file, capacity)) {
    return true;
  }
  /* Spare capacity is an optimization, not a requirement for this write. */
  return capacity != size && task_replace_file_buffer(file, size);
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

  begin_operation(file);
  size_t count = 0;
  if (request->offset < file->size) {
    count = file->size - request->offset;
    if (count > request->capacity) {
      count = request->capacity;
    }
  }
  /* Never form data + offset at EOF. RAM backing cannot be replaced or
   * modified during this copy, and cannot alias private user mappings. */
  if (count) {
    KASSERT(copy_to_user(request->address,
        (const uint8_t *)file->data + request->offset, count));
  }
  end_operation(file);

  reply.read = count;
  /* Private mappings stay stable across waits. The captured request permits
   * overlap; the reply wins over copied data. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result write_file(struct file_object *file,
    const struct file_write_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct file_write_reply reply = {.written = request->size};
  if (reply_capacity < sizeof(reply) || request->size > SIZE_MAX - request->offset) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(request->address, request->size, USER_BUFFER_READ)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  begin_operation(file);
  if (request->size) {
    size_t end = request->offset + request->size;
    if (!reserve_buffer(file, end)) {
      end_operation(file);
      return (struct syscall_result){CALL_NO_MEMORY, 0};
    }
    uint8_t *data = (uint8_t *)file->data;
    if (request->offset > file->size) {
      memset(data + file->size, 0, request->offset - file->size);
    }
    /* All fallible work is complete. The sole user task cannot change its
     * source or mappings while blocked; BSP never reads its private memory. */
    KASSERT(copy_from_user(data + request->offset, request->address, request->size));
    if (end > file->size) {
      file->size = end;
    }
  }
  end_operation(file);

  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result resize_file(struct file_object *file, size_t size)
{
  begin_operation(file);
  if (!size && file->capacity) {
    KASSERT(task_replace_file_buffer(file, 0));
  } else if (!reserve_buffer(file, size)) {
    end_operation(file);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  } else if (size > file->size) {
    /* Also clear retained capacity: truncated contents must never reappear. */
    memset((uint8_t *)file->data + file->size, 0, size - file->size);
  }
  file->size = size;
  end_operation(file);
  return (struct syscall_result){CALL_OK, 0};
}

struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  switch (operation) {
  case FILE_READ:
    required = FILE_RIGHT_READ;
    break;
  case FILE_SIZE:
    required = FILE_RIGHTS;
    break;
  case FILE_WRITE:
  case FILE_RESIZE:
    required = FILE_RIGHT_WRITE;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  union file_payload request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == FILE_READ) {
    return read_file(file, &request.read, reply_address, reply_capacity);
  }
  if (operation == FILE_WRITE || operation == FILE_RESIZE) {
    if (file->backing != FILE_RAM) {
      return (struct syscall_result){CALL_READ_ONLY, 0};
    }
    if (operation == FILE_WRITE) {
      return write_file(file, &request.write, reply_address, reply_capacity);
    }
    return resize_file(file, request.resize.size);
  }

  struct file_size_reply reply;
  if (reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  begin_operation(file);
  reply.size = file->size;
  end_operation(file);
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
