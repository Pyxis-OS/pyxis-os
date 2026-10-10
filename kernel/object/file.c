#include <abi/file.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <kernel/object/file.h>
#include <kernel/initrd.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/image_capture.h>

static const uint8_t zero_page[PAGE_SIZE];

static size_t page_count(size_t size)
{
  return size / PAGE_SIZE + (size % PAGE_SIZE != 0);
}

/* Frees every frame from page first onwards; later entries become holes. */
static void free_pages_from(struct file_object *file, size_t first)
{
  for (size_t i = first; i < file->page_capacity; ++i) {
    if (file->pages[i]) {
      pmm_free(file->pages[i], 1);
      file->pages[i] = 0;
    }
  }
}

static void release_pages(struct file_object *file)
{
  free_pages_from(file, 0);
  kfree(file->pages);
  file->pages = NULL;
  file->page_capacity = 0;
}

/* Grows the index to cover count pages, doubling when it can. New entries are
 * holes. Failure leaves the index unchanged. */
static bool reserve_index(struct file_object *file, size_t count)
{
  if (count <= file->page_capacity) {
    return true;
  }
  size_t capacity = count;
  if (file->page_capacity <= SIZE_MAX / 2 && capacity < file->page_capacity * 2) {
    capacity = file->page_capacity * 2;
  }
  phys_addr_t *pages = NULL;
  if (capacity <= SIZE_MAX / sizeof(*pages)) {
    pages = kmalloc(capacity * sizeof(*pages));
  }
  if (!pages && capacity != count && count <= SIZE_MAX / sizeof(*pages)) {
    capacity = count;
    pages = kmalloc(capacity * sizeof(*pages));
  }
  if (!pages) {
    return false;
  }
  if (file->page_capacity) {
    memcpy(pages, file->pages, file->page_capacity * sizeof(*pages));
  }
  memset(pages + file->page_capacity, 0, (capacity - file->page_capacity) * sizeof(*pages));
  kfree(file->pages);
  file->pages = pages;
  file->page_capacity = capacity;
  return true;
}

/* A FILE payload is below one page, so a READ or WRITE spans at most two. */
#define FILE_CALL_MAX_PAGES 2

/* Gives every page in [first, last] a zeroed frame. On failure frees the frames
 * this call added, leaving the file unchanged. */
static bool fill_holes(struct file_object *file, size_t first, size_t last)
{
  KASSERT(first <= last && last - first < FILE_CALL_MAX_PAGES && last < file->page_capacity);
  size_t added[FILE_CALL_MAX_PAGES];
  size_t count = 0;
  for (size_t i = first; i <= last; ++i) {
    if (file->pages[i]) {
      continue;
    }
    phys_addr_t frame = pmm_alloc(1);
    if (!frame) {
      while (count) {
        size_t page = added[--count];
        pmm_free(file->pages[page], 1);
        file->pages[page] = 0;
      }
      return false;
    }
    arch_frame_zero(frame);
    file->pages[i] = frame;
    added[count++] = i;
  }
  return true;
}

/* Copies between the file's pages and a user or kernel buffer, page by page,
 * through this CPU's scratch slot. Holes read as zeros. */
static void copy_out_pages(const struct file_object *file, size_t offset, size_t count,
    uintptr_t user, uint8_t *kernel)
{
  while (count) {
    size_t page = offset / PAGE_SIZE, within = offset % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - within < count ? PAGE_SIZE - within : count;
    phys_addr_t frame = page < file->page_capacity ? file->pages[page] : 0;
    const uint8_t *source = frame ? arch_frame_map(frame) : zero_page;
    if (kernel) {
      memcpy(kernel, source + within, chunk);
      kernel += chunk;
    } else {
      KASSERT(copy_to_user(user, source + within, chunk));
      user += chunk;
    }
    if (frame) {
      arch_frame_unmap();
    }
    offset += chunk;
    count -= chunk;
  }
}

static void copy_in_pages(struct file_object *file, size_t offset, size_t count,
    uintptr_t user, const uint8_t *kernel)
{
  while (count) {
    size_t page = offset / PAGE_SIZE, within = offset % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - within < count ? PAGE_SIZE - within : count;
    KASSERT(page < file->page_capacity && file->pages[page]);
    uint8_t *target = arch_frame_map(file->pages[page]);
    if (kernel) {
      memcpy(target + within, kernel, chunk);
      kernel += chunk;
    } else {
      KASSERT(copy_from_user(target + within, user, chunk));
      user += chunk;
    }
    arch_frame_unmap();
    offset += chunk;
    count -= chunk;
  }
}

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
    release_pages(file);
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
  if ((backing == FILE_RAM || backing == FILE_INITRD) &&
      !fs_metadata_create(&file->metadata, backing == FILE_INITRD)) {
    kfree(file);
    return NULL;
  }
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

struct file_object *file_create_snapshot(const void *data, size_t size)
{
  KASSERT(data && size);
  struct file_object *file = create_file(FILE_RAM);
  if (!file) {
    return NULL;
  }
  size_t count = page_count(size);
  bool ok = reserve_index(file, count);
  for (size_t i = 0; ok && i < count; ++i) {
    file->pages[i] = pmm_alloc(1);
    ok = file->pages[i] != 0;
  }
  if (!ok) {
    release_pages(file);
    kfree(file);
    return NULL;
  }
  /* Zero the last page first so bytes past the size start out zero. */
  arch_frame_zero(file->pages[count - 1]);
  copy_in_pages(file, 0, size, 0, data);
  file->size = size;
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

enum call_status file_ram_capture(struct file_object *file, struct image_capture *capture)
{
  KASSERT(arch_cpu_index() == 0 && file->backing == FILE_RAM && file->busy);
  enum call_status status = image_capture_allocate(file->size, capture);
  if (status != CALL_OK) {
    return status;
  }
  copy_out_pages(file, 0, capture->size, 0, (void *)capture->address);
  return CALL_OK;
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
  /* Never form data + offset at EOF. Operation ownership keeps the bytes
   * stable during this copy; they cannot alias private user mappings. */
  if (count && file->backing == FILE_RAM) {
    copy_out_pages(file, request->offset, count, data_address, NULL);
  } else if (count) {
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
  size_t first = request->offset / PAGE_SIZE, last = (end - 1) / PAGE_SIZE;
  if (!reserve_index(file, last + 1) || !fill_holes(file, first, last)) {
    file_end_operation(file);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  /* All fallible work is complete. Pages past the old size were already
   * zero, so a gap needs no clearing. The sole user task cannot change its
   * source or mappings during this syscall. */
  copy_in_pages(file, request->offset, request->size, data_address, NULL);
  if (end > file->size) {
    file->size = end;
  }
  fs_metadata_touch(&file->metadata);
  file_end_operation(file);

  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result resize_file(struct file_object *file, size_t size)
{
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  bool changed = size != file->size;
  /* Growth leaves holes. Shrinking frees whole pages past the new size and
   * zeroes the rest of its last page, so truncated bytes never reappear. */
  if (!size) {
    release_pages(file);
  } else if (size < file->size) {
    free_pages_from(file, page_count(size));
    size_t last = page_count(size) - 1, within = size % PAGE_SIZE;
    if (within && last < file->page_capacity && file->pages[last]) {
      uint8_t *tail = arch_frame_map(file->pages[last]);
      memset(tail + within, 0, PAGE_SIZE - within);
      arch_frame_unmap();
    }
  }
  file->size = size;
  if (changed) {
    fs_metadata_touch(&file->metadata);
  }
  file_end_operation(file);
  return (struct syscall_result){CALL_OK, 0};
}

static struct syscall_result query_info(struct file_object *file, uint64_t rights,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  struct file_info_reply reply = {0};
  if (request_size || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (file->backing == FILE_NPFS) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_INFO);
    pending->job.node = file->npfs;
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status == CALL_OK) {
      KASSERT(copy_to_user(reply_address, &pending->job.file_info, sizeof(reply)));
    }
    npfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_INFO);
    pending->node = file->host;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status == CALL_OK) {
      KASSERT(copy_to_user(reply_address, &pending->info, sizeof(reply)));
    }
    hostfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  reply.valid = FILE_INFO_SIZE_VALID;
  reply.size = file->size;
  fs_metadata_info(&file->metadata, &reply);
  file_end_operation(file);
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
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
  case FILE_INFO:
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
  if (operation == FILE_INFO) {
    return query_info(file, rights, request_size, reply_address, reply_capacity);
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
