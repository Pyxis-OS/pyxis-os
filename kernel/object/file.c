#include <abi/file.h>
#include <arch/smp.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/object/file.h>
#include <kernel/initrd.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/service/profile.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static void destroy_file(struct kernel_object *object)
{
  struct file_object *file = (struct file_object *)object;
  KASSERT(!file->busy && !file->first_waiter);
  if (file->backing == FILE_NPFS) {
    npfs_retire(file->npfs);
    return;
  }
  if (file->backing == FILE_HOST) {
    hostfs_retire(file->host);
    return;
  }
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

struct file_object *file_create_snapshot(void *data, size_t size)
{
  KASSERT(data && size);
  struct file_object *file = create_file(FILE_RAM);
  if (file) {
    file->data = data;
    file->size = size;
    file->capacity = size;
  }
  return file;
}

struct file_object *file_create_host(struct hostfs_node *host)
{
  struct file_object *file = create_file(FILE_HOST);
  if (file) {
    file->host = host;
  }
  return file;
}

void file_init_npfs(struct file_object *file, struct npfs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && file && node);
  *file = (struct file_object){.backing = FILE_NPFS, .npfs = node};
  atomic_init(&file->locked, false);
  object_init(&file->object, OBJECT_FILE, destroy_file);
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

bool file_begin_operation(struct file_object *file)
{
  KASSERT(file->backing == FILE_INITRD || file->backing == FILE_RAM);
  lock_file(file);
  if (task_stop_requested()) {
    unlock_file(file);
    return false;
  }
  if (!file->busy) {
    file->busy = true;
    unlock_file(file);
    return true;
  }

  struct task_wait_link *waiter = task_wait_link_prepare();
  struct task_wait *wait = waiter->wait;
  if (file->last_waiter) {
    file->last_waiter->next = waiter;
  } else {
    file->first_waiter = waiter;
  }
  file->last_waiter = waiter;
  unlock_file(file);
  bool resumed = task_wait_sleep_interruptible(wait);
  lock_file(file);
  bool acquired = waiter->wait == NULL;
  if (!acquired) {
    struct task_wait_link **link = &file->first_waiter;
    struct task_wait_link *previous = NULL;
    while (*link != waiter) {
      KASSERT(*link);
      previous = *link;
      link = &(*link)->next;
    }
    *link = waiter->next;
    if (file->last_waiter == waiter) {
      file->last_waiter = previous;
    }
    waiter->next = NULL;
    waiter->wait = NULL;
  }
  bool stopped = !resumed || task_stop_requested();
  unlock_file(file);
  if (stopped) {
    if (acquired) {
      file_end_operation(file);
    }
    return false;
  }
  /* The previous owner handed busy directly to us before waking. */
  KASSERT(acquired);
  return true;
}

void file_end_operation(struct file_object *file)
{
  KASSERT(file->backing == FILE_INITRD || file->backing == FILE_RAM);
  lock_file(file);
  struct task_wait_link *waiter = file->first_waiter;
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

static bool file_replace_buffer(struct file_object *file, size_t capacity,
    struct file_buffer_profile *profile)
{
  KASSERT(arch_cpu_index() == 0 && file->backing == FILE_RAM && file->busy);
  KASSERT(!capacity || capacity >= file->size);
  void *data = NULL;
  if (capacity) {
    if (profile) {
      profile->allocation_started = arch_monotonic_ns();
    }
    data = kmalloc(capacity);
    if (profile) {
      profile->allocation_ended = arch_monotonic_ns();
    }
    if (!data) {
      return false;
    }
    if (file->size) {
      if (profile) {
        profile->copy_started = arch_monotonic_ns();
      }
      memcpy(data, file->data, file->size);
      if (profile) {
        profile->copy_ended = arch_monotonic_ns();
        profile->copied_bytes = file->size;
      }
    }
  }
  if (profile) {
    profile->release_started = arch_monotonic_ns();
  }
  kfree((void *)file->data);
  if (profile) {
    profile->release_ended = arch_monotonic_ns();
  }
  file->data = data;
  file->capacity = capacity;
  return true;
}

void file_replace_published(struct file_replace_request *request)
{
  KASSERT(request && request->request.state == BSP_REQUEST_PREPARED);
  if (request->profile.active) {
    request->profile.published_ns = arch_monotonic_ns();
  }
}

void file_replace_execute(struct file_replace_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request && request->file);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING);
  if (request->profile.active) {
    request->profile.service_started_ns = arch_monotonic_ns();
  }
  request->result = file_replace_buffer(request->file, request->capacity,
      request->profile.active ? &request->profile.buffer : NULL);
  if (request->profile.active) {
    request->profile.service_ended_ns = arch_monotonic_ns();
  }
  request->file = NULL;
}

static void finish_file_profile(struct profile_file_snapshot *stats,
    const struct file_replace_request *request)
{
  uint64_t resumed = arch_monotonic_ns();
  const struct file_replace_profile *sample = &request->profile;
  const struct file_buffer_profile *service = &sample->buffer;
  profile_add(&stats->flags, &stats->requests, 1);
  profile_add(&stats->flags, request->result ? &stats->successes : &stats->failures, 1);
  profile_add(&stats->flags, &stats->requested_capacity, request->capacity);
  profile_add(&stats->flags, &stats->copied_bytes, service->copied_bytes);
  profile_duration_add(&stats->flags, &stats->publication, sample->started_ns,
      sample->published_ns);
  profile_duration_add(&stats->flags, &stats->queue, sample->published_ns,
      sample->service_started_ns);
  profile_duration_add(&stats->flags, &stats->service, sample->service_started_ns,
      sample->service_ended_ns);
  profile_duration_add(&stats->flags, &stats->resume, sample->service_ended_ns, resumed);
  profile_duration_add(&stats->flags, &stats->total, sample->started_ns, resumed);
  profile_duration_add(&stats->flags, &stats->allocation, service->allocation_started,
      service->allocation_ended);
  profile_duration_add(&stats->flags, &stats->copy, service->copy_started,
      service->copy_ended);
  profile_duration_add(&stats->flags, &stats->release, service->release_started,
      service->release_ended);
}

static bool replace_buffer(struct file_object *file, size_t capacity)
{
  struct profile_file_snapshot *profile = profile_file_current();
  bool profiled = profile->flags & PROFILE_ACTIVE;
  uint64_t started = profiled ? arch_monotonic_ns() : 0;
  struct file_replace_request *request =
      (struct file_replace_request *)bsp_request_prepare(BSP_SERVICE_FILE_REPLACE);
  request->file = file;
  request->capacity = capacity;
  request->result = false;
  request->profile = (struct file_replace_profile){.active = profiled, .started_ns = started};

  bsp_request_submit_and_wait(&request->request);
  if (profiled) {
    finish_file_profile(profile, request);
  }
  bool result = request->result;
  bsp_request_release(&request->request);
  return result;
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
  if (replace_buffer(file, capacity)) {
    return true;
  }
  /* Spare capacity is an optimization, not a requirement for this write. */
  return capacity != size && replace_buffer(file, size);
}

static struct syscall_result read_file(struct file_object *file, uint64_t rights,
    const struct file_read_request *request, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct file_read_reply reply;
  if (request->capacity > FILE_READ_MAX_BYTES ||
      reply_capacity < sizeof(reply) + request->capacity) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply) + request->capacity,
      USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  uintptr_t data_address = reply_address + sizeof(reply);

  if (file->backing == FILE_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_READ);
    pending->job.node = file->npfs;
    pending->job.rights = rights;
    pending->job.offset = request->offset;
    pending->job.count = request->capacity;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    KASSERT(pending->job.count <= request->capacity);
    reply.read = pending->job.count;
    KASSERT(copy_to_user(data_address, pending->job.data, pending->job.count));
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    npfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply) + reply.read};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_READ);
    pending->node = file->host;
    pending->offset = request->offset;
    pending->count = request->capacity < VIRTIO_FS_READ_MAX ? request->capacity : VIRTIO_FS_READ_MAX;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.read = pending->count;
    KASSERT(copy_to_user(data_address, pending->data, pending->count));
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply) + reply.read};
  }

  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
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
    KASSERT(copy_to_user(data_address,
        (const uint8_t *)file->data + request->offset, count));
  }
  file_end_operation(file);

  reply.read = count;
  /* Private mappings stay stable across waits. The request is already captured,
   * so its storage may overlap the reply. */
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply) + reply.read};
}

static struct syscall_result write_file(struct file_object *file, uint64_t rights,
    const struct file_write_request *request, uintptr_t data_address,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct file_write_reply reply = {.written = request->size};
  if (reply_capacity < sizeof(reply) || request->size > SIZE_MAX - request->offset) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      !user_buffer_check(data_address, request->size, USER_BUFFER_READ)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  if (file->backing == FILE_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_WRITE);
    pending->job.node = file->npfs;
    pending->job.rights = rights;
    pending->job.offset = request->offset;
    pending->job.count = request->size;
    KASSERT(pending->job.count <= sizeof(pending->job.data));
    KASSERT(copy_from_user(pending->job.data, data_address, pending->job.count));
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    KASSERT(pending->job.count <= request->size);
    reply.written = pending->job.count;
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    npfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (!request->size) {
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_WRITE);
    pending->node = file->host;
    pending->offset = request->offset;
    pending->count = request->size < VIRTIO_FS_WRITE_MAX ? request->size : VIRTIO_FS_WRITE_MAX;
    KASSERT(copy_from_user(pending->data, data_address, pending->count));
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.written = pending->count;
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (file->backing != FILE_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }

  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  size_t end = request->offset + request->size;
  if (!reserve_buffer(file, end)) {
    file_end_operation(file);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  uint8_t *data = (uint8_t *)file->data;
  if (request->offset > file->size) {
    memset(data + file->size, 0, request->offset - file->size);
  }
  /* All fallible work is complete. The sole user task cannot change its
   * source or mappings while blocked; BSP never reads its private memory. */
  KASSERT(copy_from_user(data + request->offset, data_address, request->size));
  if (end > file->size) {
    file->size = end;
  }
  file_end_operation(file);

  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result resize_file(struct file_object *file, size_t size)
{
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (!size && file->capacity) {
    KASSERT(replace_buffer(file, 0));
  } else if (!reserve_buffer(file, size)) {
    file_end_operation(file);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  } else if (size > file->size) {
    /* Also clear retained capacity: truncated contents must never reappear. */
    memset((uint8_t *)file->data + file->size, 0, size - file->size);
  }
  file->size = size;
  file_end_operation(file);
  return (struct syscall_result){CALL_OK, 0};
}

struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (file->backing == FILE_NPFS && (rights & ~FILE_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
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
  case FILE_SYNC:
    required = FILE_RIGHT_WRITE;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }

  if (operation == FILE_READ) {
    struct file_read_request request;
    if (request_size != sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return read_file(file, rights, &request, reply_address, reply_capacity);
  }
  if (operation == FILE_WRITE) {
    struct file_write_request request;
    if (request_size < sizeof(request) || request_size > FILE_PAYLOAD_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (request.size != request_size - sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    return write_file(file, rights, &request, request_address + sizeof(request),
        reply_address, reply_capacity);
  }
  if (operation == FILE_SYNC) {
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (file->backing == FILE_NPFS) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_SYNC);
      pending->job.node = file->npfs;
      pending->job.rights = rights;
      npfs_request_submit_and_wait(pending);
      enum call_status status = pending->job.status;
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (file->backing == FILE_HOST) {
      struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_SYNC);
      pending->node = file->host;
      hostfs_request_submit_and_wait(pending);
      enum call_status status = pending->status;
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    return (struct syscall_result){file->backing == FILE_RAM ? CALL_OK : CALL_READ_ONLY, 0};
  }
  if (operation == FILE_RESIZE) {
    struct file_resize_request request;
    if (request_size != sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (file->backing == FILE_NPFS) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_RESIZE);
      pending->job.node = file->npfs;
      pending->job.rights = rights;
      pending->job.offset = request.size;
      npfs_request_submit_and_wait(pending);
      enum call_status status = pending->job.status;
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (file->backing == FILE_HOST) {
      struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_RESIZE);
      pending->node = file->host;
      pending->offset = request.size;
      hostfs_request_submit_and_wait(pending);
      enum call_status status = pending->status;
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    if (file->backing != FILE_RAM) {
      return (struct syscall_result){CALL_READ_ONLY, 0};
    }
    return resize_file(file, request.size);
  }

  struct file_size_reply reply;
  if (request_size || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (file->backing == FILE_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_SIZE);
    pending->job.node = file->npfs;
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.size = pending->job.offset;
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    npfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_SIZE);
    pending->node = file->host;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.size = pending->offset;
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  } else {
    if (!file_begin_operation(file)) {
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    reply.size = file->size;
    file_end_operation(file);
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
