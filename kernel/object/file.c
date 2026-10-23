#include <abi/file.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <kernel/object/file.h>
#include <kernel/object/retention.h>
#include <kernel/completion.h>
#include <kernel/initrd.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <kernel/user/image_capture.h>

struct file_failure {
  uint64_t outcome;
  size_t detail_size;
  char detail[RESOURCE_DETAIL_MAX_BYTES + 1];
};

static const struct file_failure too_large = {
  .outcome = RESOURCE_TOO_LARGE,
  .detail_size = sizeof("too large to retain; write it to a file instead") - 1,
  .detail = "too large to retain; write it to a file instead",
};

static enum call_status outcome_status(uint64_t outcome)
{
  switch (outcome) {
  case RESOURCE_OK: return CALL_OK;
  case RESOURCE_NOT_FOUND: return CALL_NOT_FOUND;
  case RESOURCE_DENIED: return CALL_DENIED;
  case RESOURCE_IO_ERROR: return CALL_IO;
  case RESOURCE_TOO_LARGE: return CALL_FILE_TOO_LARGE;
  case RESOURCE_ABORTED: return CALL_ABANDONED;
  case RESOURCE_UNCERTAIN: return CALL_OUTCOME_UNKNOWN;
  default: return CALL_IO;
  }
}

enum call_status file_failure_status(const struct file_object *file)
{
  const struct file_failure *failure = atomic_load_explicit(&file->failure, memory_order_acquire);
  return failure ? outcome_status(failure->outcome) : CALL_OK;
}

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
      if (file->erase_backing) {
        void *page = arch_frame_map(file->pages[i]);
        memzero_explicit(page, PAGE_SIZE);
        arch_frame_unmap();
      }
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
  KASSERT(first <= last && last - first <= FILE_TRANSFER_MAX_BYTES / PAGE_SIZE && last < file->page_capacity);
  size_t added[FILE_TRANSFER_MAX_BYTES / PAGE_SIZE + 1];
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

/* Copies through this CPU's scratch slot; holes read as zeros. The destination
 * is kernel storage or a fully validated user range held under a mapping lease. */
static void copy_out_pages(const struct file_object *file, size_t offset, size_t count,
    uint8_t *destination)
{
  while (count) {
    size_t page = offset / PAGE_SIZE, within = offset % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - within < count ? PAGE_SIZE - within : count;
    phys_addr_t frame = page < file->page_capacity ? file->pages[page] : 0;
    const uint8_t *source = frame ? arch_frame_map(frame) : zero_page;
    memcpy(destination, source + within, chunk);
    destination += chunk;
    if (frame) {
      arch_frame_unmap();
    }
    offset += chunk;
    count -= chunk;
  }
}

static void copy_in_pages(struct file_object *file, size_t offset, size_t count,
    const uint8_t *kernel)
{
  while (count) {
    size_t page = offset / PAGE_SIZE, within = offset % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - within < count ? PAGE_SIZE - within : count;
    KASSERT(page < file->page_capacity && file->pages[page]);
    uint8_t *target = arch_frame_map(file->pages[page]);
    memcpy(target + within, kernel, chunk);
    kernel += chunk;
    arch_frame_unmap();
    offset += chunk;
    count -= chunk;
  }
}

static void destroy_file(struct kernel_object *object)
{
  struct file_object *file = (struct file_object *)object;
  KASSERT(!file->busy && !file->first_waiter);
  if (file->backing == FILE_FAT32) {
    fat32_retire_node(file->fat32);
    return;
  }
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
  if (file->retention) {
    retention_release(file->retention, file->retained_bytes);
    object_release(&file->retention->object);
  }
  if (file->failure_owned) {
    kfree((void *)atomic_load_explicit(&file->failure, memory_order_relaxed));
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
  atomic_init(&file->failure, NULL);
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

struct file_object *file_create_retained(struct retention_account *account)
{
  struct file_object *file = file_create_ram();
  if (file) {
    if (!object_retain(&account->object)) {
      object_release(&file->object);
      return NULL;
    }
    file->retention = account;
  }
  return file;
}

struct file_object *file_create_failure(struct retention_account *account,
    uint64_t outcome, const char *detail, size_t detail_size)
{
  KASSERT(outcome && outcome < RESOURCE_OUTCOME_COUNT && detail_size <= RESOURCE_DETAIL_MAX_BYTES);
  struct file_object *file = file_create_retained(account);
  if (!file) {
    return NULL;
  }
  struct file_failure *failure = kmalloc(sizeof(*failure));
  if (!failure) {
    object_release(&file->object);
    return NULL;
  }
  *failure = (struct file_failure){.outcome = outcome, .detail_size = detail_size};
  if (detail_size) {
    memcpy(failure->detail, detail, detail_size);
  }
  file->failure_owned = true;
  file->immutable_snapshot = true;
  atomic_store_explicit(&file->failure, failure, memory_order_release);
  return file;
}

/* Operation ownership, no file/account guard held across allocation or freeing.
 * Reservations can exceed size only until this operation commits or unwinds. */
static bool reserve_length(struct file_object *file, size_t size)
{
  if (!file->retention || size <= file->retained_bytes) {
    return true;
  }
  if (!retention_reserve(file->retention, file->retained_bytes, size)) {
    return false;
  }
  file->retained_bytes = size;
  return true;
}

static void release_length(struct file_object *file, size_t size)
{
  if (file->retention) {
    KASSERT(size <= file->retained_bytes);
    retention_release(file->retention, file->retained_bytes - size);
    file->retained_bytes = size;
  }
}

static void fail_retention(struct file_object *file)
{
  KASSERT(file->busy && file->retention && !file->failure_owned);
  release_pages(file);
  file->size = 0;
  release_length(file, 0);
  atomic_store_explicit(&file->failure, &too_large, memory_order_release);
}

static struct file_object *prepare_snapshot(size_t size, bool erase_backing)
{
  KASSERT(arch_cpu_index() == 0);
  struct file_object *file = create_file(FILE_RAM);
  if (!file) {
    return NULL;
  }
  file->erase_backing = erase_backing;
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
  if (count) {
    arch_frame_zero(file->pages[count - 1]);
  }
  file->size = size;
  return file;
}

struct file_object *file_prepare_snapshot(size_t size)
{
  return prepare_snapshot(size, false);
}

struct file_object *file_prepare_secret_snapshot(size_t size)
{
  return prepare_snapshot(size, true);
}

void file_fill_snapshot_size(struct file_object *file, const void *data, size_t size)
{
  KASSERT(arch_cpu_index() == 0 && file->backing == FILE_RAM && !file->immutable_snapshot);
  KASSERT(!file->busy && size <= file->size && (data || !size));
  free_pages_from(file, page_count(size));
  if (size % PAGE_SIZE) {
    uint8_t *tail = arch_frame_map(file->pages[page_count(size) - 1]);
    memset(tail + size % PAGE_SIZE, 0, PAGE_SIZE - size % PAGE_SIZE);
    arch_frame_unmap();
  }
  file->size = size;
  copy_in_pages(file, 0, size, data);
  file->immutable_snapshot = true;
}

void file_fill_snapshot(struct file_object *file, const void *data)
{
  file_fill_snapshot_size(file, data, file->size);
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
  atomic_init(&file->failure, NULL);
  object_init(&file->object, OBJECT_FILE, destroy_file);
}

void file_init_fat32(struct file_object *file, struct fat32_node *node)
{
  KASSERT(arch_cpu_index() == 0);
  *file = (struct file_object){.backing = FILE_FAT32, .fat32 = node};
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

bool file_cached_length(struct file_object *file, uint64_t *length)
{
  if (!length) {
    return false;
  }
  *length = 0;
  if (file_failure_status(file) != CALL_OK) {
    return false;
  }
  if (file->backing == FILE_INITRD || file->immutable_snapshot) {
    *length = file->size;
    return true;
  }
  if (file->backing != FILE_RAM ||
      atomic_exchange_explicit(&file->locked, true, memory_order_acquire)) {
    return false;
  }
  bool known = !file->busy;
  if (known) {
    *length = file->size;
  }
  unlock_file(file);
  return known;
}

void file_description(struct file_object *file, struct resource_description *description,
    uint64_t *properties)
{
  const struct file_failure *failure = atomic_load_explicit(&file->failure, memory_order_acquire);
  *description = (struct resource_description){.flags = RESOURCE_REREADABLE | RESOURCE_COMPLETE};
  if (failure) {
    description->outcome = failure->outcome;
    description->detail_size = failure->detail_size;
    memcpy(description->detail, failure->detail, failure->detail_size);
    *properties |= DESCRIBE_IMMUTABLE_SNAPSHOT;
  } else {
    if (file_cached_length(file, &description->length)) {
      description->flags |= RESOURCE_LENGTH_KNOWN;
    }
    if (file->immutable_snapshot) {
      *properties |= DESCRIBE_IMMUTABLE_SNAPSHOT;
    }
  }
}

bool file_begin_operation(struct file_object *file)
{
  KASSERT(file->backing == FILE_INITRD || file->backing == FILE_RAM);
  lock_file(file);
  if (task_stop_requested() || task_operation_denied()) {
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
  bool stopped = !resumed || (task_stop_requested() || task_operation_denied());
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
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    return failure;
  }
  enum call_status status = image_capture_allocate(file->size, capture);
  if (status != CALL_OK) {
    return status;
  }
  copy_out_pages(file, 0, capture->size, (void *)capture->address);
  return CALL_OK;
}

static struct syscall_result read_memory_file(struct file_object *file,
    const struct file_read_request *request, uintptr_t reply_address,
    const void *prefix, size_t prefix_size)
{
  uintptr_t data_address = reply_address + prefix_size + sizeof(struct file_read_reply);
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    file_end_operation(file);
    return (struct syscall_result){failure, 0};
  }
  size_t count = 0;
  if (request->offset < file->size) {
    count = file->size - request->offset;
    if (count > request->capacity) {
      count = request->capacity;
    }
  }
  struct user_buffer_lease lease;
  if (!user_buffer_lease_acquire(&lease)) {
    file_end_operation(file);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!user_buffer_check_leased(&lease, reply_address, prefix_size + sizeof(struct file_read_reply) + count, USER_BUFFER_WRITE)) {
    user_buffer_lease_release(&lease);
    file_end_operation(file);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!task_operation_start()) {
    user_buffer_lease_release(&lease);
    file_end_operation(file);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  /* Ownership keeps file bytes stable; the short lease keeps the complete
   * output mapped across all page copies. Neither boundary schedules. */
  if (count && file->backing == FILE_RAM) {
    copy_out_pages(file, request->offset, count, (uint8_t *)data_address);
  } else if (count) {
    memcpy((void *)data_address, (const uint8_t *)file->data + request->offset, count);
  }
  struct file_read_reply reply = {.read = count};
  memcpy((void *)(reply_address + prefix_size), &reply, sizeof(reply));
  if (prefix_size) {
    memcpy((void *)reply_address, prefix, prefix_size);
  }
  user_buffer_lease_release(&lease);
  file_end_operation(file);
  return (struct syscall_result){CALL_OK, prefix_size + sizeof(reply) + reply.read};
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

  if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_READ);
    pending->job.node = file->npfs;
    pending->job.fat_node = file->fat32;
    if (file->backing == FILE_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
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
    if (!copy_reply_to_user(reply_address, &reply, sizeof(reply),
        data_address, pending->job.data, pending->job.count)) {
      npfs_request_release(pending);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
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
    if (!copy_reply_to_user(reply_address, &reply, sizeof(reply),
        data_address, pending->data, pending->count)) {
      hostfs_request_release(pending);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    hostfs_request_release(pending);
    return (struct syscall_result){CALL_OK, sizeof(reply) + reply.read};
  }

  return read_memory_file(file, request, reply_address, NULL, 0);
}

struct syscall_result file_read_captured(struct file_object *file,
    const struct file_read_request *request, uintptr_t reply_address,
    const void *prefix, size_t prefix_size)
{
  KASSERT(file->backing == FILE_RAM && file->immutable_snapshot);
  KASSERT(request->capacity <= FILE_READ_MAX_BYTES);
  return read_memory_file(file, request, reply_address, prefix, prefix_size);
}

static struct syscall_result write_file(struct file_object *file, uint64_t rights,
    const struct file_write_request *request, uintptr_t data_address,
    const void *captured_data, uintptr_t reply_address, size_t reply_capacity)
{
  struct file_write_reply reply = {.written = request->size};
  if (reply_capacity < sizeof(reply) || request->size > SIZE_MAX - request->offset) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(reply), USER_BUFFER_WRITE) ||
      (!captured_data && !user_buffer_check(data_address, request->size, USER_BUFFER_READ))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_WRITE);
    pending->job.node = file->npfs;
    pending->job.fat_node = file->fat32;
    if (file->backing == FILE_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    pending->job.offset = request->offset;
    pending->job.count = request->size;
    KASSERT(pending->job.count <= sizeof(pending->job.data));
    if (!copy_from_user(pending->job.data, data_address, pending->job.count)) {
      pending->job = (struct npfs_job){0};
      bsp_request_cancel_prepared(&pending->request);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    KASSERT(pending->job.count <= request->size);
    reply.written = pending->job.count;
    bool copied = copy_to_user(reply_address, &reply, sizeof(reply));
    npfs_request_release(pending);
    if (!copied) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    return (struct syscall_result){failure, 0};
  }
  if (file->immutable_snapshot) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  if (!request->size) {
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_WRITE);
    pending->node = file->host;
    pending->offset = request->offset;
    pending->count = request->size < VIRTIO_FS_WRITE_MAX ? request->size : VIRTIO_FS_WRITE_MAX;
    if (!copy_from_user(pending->data, data_address, pending->count)) {
      pending->node = NULL;
      bsp_request_cancel_prepared(&pending->request);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status != CALL_OK) {
      hostfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.written = pending->count;
    bool copied = copy_to_user(reply_address, &reply, sizeof(reply));
    hostfs_request_release(pending);
    if (!copied) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (file->backing != FILE_RAM) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }

  uint8_t *data = kmalloc(request->size);
  if (!data) {
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  if (captured_data) {
    memcpy(data, captured_data, request->size);
  } else if (!copy_from_user(data, data_address, request->size)) {
    kfree(data);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!file_begin_operation(file)) {
    kfree(data);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  failure = file_failure_status(file);
  if (failure != CALL_OK) {
    file_end_operation(file);
    kfree(data);
    return (struct syscall_result){failure, 0};
  }
  if (file->immutable_snapshot) {
    file_end_operation(file);
    kfree(data);
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  size_t old_charge = file->retained_bytes;
  size_t end = request->offset + request->size;
  if (!reserve_length(file, end > file->size ? end : file->size)) {
    bool started = task_operation_start();
    if (started) {
      fail_retention(file);
    }
    file_end_operation(file);
    kfree(data);
    return (struct syscall_result){started ? CALL_FILE_TOO_LARGE : CALL_DENIED, 0};
  }
  size_t first = request->offset / PAGE_SIZE, last = (end - 1) / PAGE_SIZE;
  bool existing[FILE_CALL_MAX_PAGES];
  for (size_t page = first; page <= last; ++page) {
    existing[page - first] = page < file->page_capacity && file->pages[page];
  }
  if (!reserve_index(file, last + 1) || !fill_holes(file, first, last)) {
    release_length(file, old_charge);
    file_end_operation(file);
    kfree(data);
    return (struct syscall_result){CALL_NO_MEMORY, 0};
  }
  /* Pages past the old size are zero; the captured input cannot change while
   * ownership or index/frame allocation waits. */
  if (!task_operation_start()) {
    for (size_t page = first; page <= last; ++page) {
      if (!existing[page - first]) {
        pmm_free(file->pages[page], 1);
        file->pages[page] = 0;
      }
    }
    release_length(file, old_charge);
    file_end_operation(file);
    kfree(data);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  copy_in_pages(file, request->offset, request->size, data);
  if (end > file->size) {
    file->size = end;
  }
  fs_metadata_touch(&file->metadata);
  file_end_operation(file);
  kfree(data);

  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static void resize_owned(struct file_object *file, size_t size)
{
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
}

static enum call_status resize_accounted(struct file_object *file, size_t size)
{
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    return failure;
  }
  if (file->immutable_snapshot) {
    return CALL_READ_ONLY;
  }
  if (!reserve_length(file, size)) {
    fail_retention(file);
    return CALL_FILE_TOO_LARGE;
  }
  resize_owned(file, size);
  release_length(file, size);
  return CALL_OK;
}

static struct syscall_result resize_file(struct file_object *file, size_t size)
{
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (!task_operation_start()) {
    file_end_operation(file);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  enum call_status status = resize_accounted(file, size);
  file_end_operation(file);
  return (struct syscall_result){status, 0};
}

struct syscall_result file_output_write(struct file_object *file,
    const struct file_write_request *request, const void *data,
    uintptr_t reply_address, size_t reply_capacity)
{
  KASSERT(file->backing == FILE_RAM && request->size <= FILE_WRITE_MAX_BYTES);
  return write_file(file, FILE_RIGHT_WRITE, request, 0, data,
      reply_address, reply_capacity);
}

struct syscall_result file_output_resize(struct file_object *file, size_t size)
{
  KASSERT(file->backing == FILE_RAM);
  return resize_file(file, size);
}

#define FILE_TRANSFER_MAX_PAGES (FILE_TRANSFER_MAX_BYTES / PAGE_SIZE + 1)

struct file_transfer_state {
  struct file_object *memory;
  size_t old_charge, first, pages;
  bool existing[FILE_TRANSFER_MAX_PAGES];
  bool write_memory, charged;
};

enum call_status file_transfer_capture(const struct file_transfer_request *request,
    struct capability_reference *source)
{
  *source = (struct capability_reference){0};
  if (request->size > FILE_TRANSFER_MAX_BYTES ||
      request->size > SIZE_MAX - request->source_offset ||
      request->size > SIZE_MAX - request->destination_offset) {
    return CALL_BAD_REQUEST;
  }
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      request->source, FILE_RIGHT_READ, 0, source);
  if (result == CAP_OK) {
    result = capability_reference_resolve(source);
  }
  enum call_status status = result == CAP_OK ? CALL_OK : result == CAP_BAD_HANDLE ?
      CALL_BAD_HANDLE : result == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED;
  if (status == CALL_OK && (source->object->type != OBJECT_FILE || source->transport)) {
    status = CALL_BAD_OPERATION;
  }
  if (status != CALL_OK) {
    capability_release(source);
  }
  return status;
}

enum call_status file_transfer_job_init(struct npfs_job *job, struct file_object *destination,
    uint64_t rights, struct capability_reference *source,
    const struct file_transfer_request *request, struct process_control *actor)
{
  if (!(rights & FILE_RIGHT_WRITE) || !(source->rights & FILE_RIGHT_READ)) {
    return CALL_DENIED;
  }
  struct file_object *file = (struct file_object *)source->object;
  if (source->object->type != OBJECT_FILE || file == destination) {
    return CALL_BAD_OPERATION;
  }
  bool to_fat = destination->backing == FILE_FAT32 &&
      (file->backing == FILE_RAM || file->backing == FILE_INITRD);
  bool from_fat = file->backing == FILE_FAT32 && destination->backing == FILE_RAM;
  if (!to_fat && !from_fat) {
    return CALL_BAD_OPERATION;
  }
  if (!object_retain(&destination->object)) {
    return CALL_LIMIT;
  }
  struct file_object *memory = to_fat ? file : destination;
  lock_file(memory);
  if (memory->busy) {
    unlock_file(memory);
    object_release(&destination->object);
    return CALL_BUSY;
  }
  memory->busy = true;
  unlock_file(memory);
  job->transfer_memory = memory;
  job->operation = to_fat ? NPFS_WRITE : NPFS_READ;
  job->fat_node = to_fat ? destination->fat32 : file->fat32;
  job->fat_actor = actor;
  job->rights = to_fat ? rights : source->rights;
  job->offset = to_fat ? request->destination_offset : request->source_offset;
  job->count = request->size;
  job->transfer_offset = to_fat ? request->source_offset : request->destination_offset;
  job->transfer_source = *source;
  *source = (struct capability_reference){0};
  job->transfer_destination = destination;
  return CALL_OK;
}

enum call_status file_transfer_prepare(struct npfs_job *job)
{
  if (!job->transfer_destination) {
    return CALL_OK;
  }
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  struct file_object *memory = job->transfer_memory;
  KASSERT(memory && memory->busy);
  struct file_transfer_state *state = kmalloc(sizeof(*state));
  if (!state) {
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  *state = (struct file_transfer_state){.memory = memory,
    .write_memory = job->operation == NPFS_READ, .old_charge = memory->retained_bytes};
  job->transfer = state;
  enum call_status status = file_failure_status(memory);
  if (status == CALL_OK && state->write_memory && memory->immutable_snapshot) {
    status = CALL_READ_ONLY;
  }
  if (status == CALL_OK && !state->write_memory) {
    size_t available = job->transfer_offset < memory->size ?
        memory->size - job->transfer_offset : 0;
    if (job->count > available) {
      job->count = available;
    }
  }
  if (status == CALL_OK && job->count) {
    job->transfer_data = kmalloc(job->count);
    job->transfer_capacity = job->transfer_data ? job->count : 0;
    if (!job->transfer_data) {
      status = CALL_NO_MEMORY;
    }
  }
  if (status == CALL_OK && job->count && state->write_memory) {
    size_t end = job->transfer_offset + job->count;
    if (!reserve_length(memory, end > memory->size ? end : memory->size)) {
      fail_retention(memory);
      status = CALL_FILE_TOO_LARGE;
    } else {
      state->charged = true;
      state->first = job->transfer_offset / PAGE_SIZE;
      size_t last = (end - 1) / PAGE_SIZE;
      state->pages = last - state->first + 1;
      KASSERT(state->pages <= FILE_TRANSFER_MAX_PAGES);
      for (size_t i = 0; i < state->pages; ++i) {
        size_t page = state->first + i;
        state->existing[i] = page < memory->page_capacity && memory->pages[page];
      }
      if (!reserve_index(memory, last + 1) || !fill_holes(memory, state->first, last)) {
        status = CALL_NO_MEMORY;
      }
    }
  }
  if (status == CALL_OK && job->count && !state->write_memory) {
    if (memory->backing == FILE_RAM) {
      copy_out_pages(memory, job->transfer_offset, job->count, job->transfer_data);
    } else {
      memcpy(job->transfer_data, (const uint8_t *)memory->data + job->transfer_offset, job->count);
    }
  }
  cpu_restore_interrupts(flags);
  return status;
}

void file_transfer_finish(struct npfs_job *job)
{
  if (!job->transfer_destination) {
    return;
  }
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  struct file_transfer_state *state = job->transfer;
  if (state) {
    struct file_object *memory = state->memory;
    if (state->write_memory && job->status == CALL_OK && job->count) {
      copy_in_pages(memory, job->transfer_offset, job->count, job->transfer_data);
      size_t end = job->transfer_offset + job->count;
      if (memory->size < end) {
        memory->size = end;
      }
      fs_metadata_touch(&memory->metadata);
    }
    if (state->write_memory && state->charged) {
      size_t kept = job->status == CALL_OK && job->count ?
          page_count(job->transfer_offset + job->count) : 0;
      for (size_t i = 0; i < state->pages; ++i) {
        size_t page = state->first + i;
        if (!state->existing[i] && page < memory->page_capacity && memory->pages[page] &&
            (job->status != CALL_OK || !job->count || page >= kept)) {
          pmm_free(memory->pages[page], 1);
          memory->pages[page] = 0;
        }
      }
      release_length(memory, job->status == CALL_OK ? memory->size : state->old_charge);
    }
    kfree(state);
    job->transfer = NULL;
  }
  kfree(job->transfer_data);
  job->transfer_data = NULL;
  job->transfer_capacity = 0;
  if (job->status == CALL_OK) {
    job->progress = job->count;
  } else {
    job->count = 0;
  }
  file_end_operation(job->transfer_memory);
  job->transfer_memory = NULL;
  if (job->status != CALL_OK) {
    fat32_note_call_failure(job->transfer_source.object);
    fat32_note_call_failure(&job->transfer_destination->object);
  }
  capability_release(&job->transfer_source);
  object_release(&job->transfer_destination->object);
  job->transfer_destination = NULL;
  cpu_restore_interrupts(flags);
}

static struct syscall_result transfer_file(struct file_object *file, uint64_t rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct file_transfer_request input;
  if (request_size != sizeof(input) || reply_capacity < sizeof(struct file_transfer_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input, request_address, sizeof(input)) ||
      !user_buffer_check(reply_address, sizeof(struct file_transfer_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reference source;
  enum call_status status = file_transfer_capture(&input, &source);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct npfs_request *pending = npfs_request_prepare(NPFS_READ);
  status = file_transfer_job_init(&pending->job, file, rights, &source, &input,
      process_current()->control);
  if (status != CALL_OK) {
    fat32_note_call_failure(source.object);
  }
  capability_release(&source);
  if (status != CALL_OK) {
    bsp_request_cancel_prepared(&pending->request);
    return (struct syscall_result){status, 0};
  }
  npfs_request_submit_and_wait(pending);
  status = pending->job.status;
  struct file_transfer_reply reply = {.transferred = pending->job.progress};
  bool copied = status != CALL_OK || copy_to_user(reply_address, &reply, sizeof(reply));
  npfs_request_release(pending);
  return (struct syscall_result){copied ? status : CALL_BAD_BUFFER,
      copied && status == CALL_OK ? sizeof(reply) : 0};
}

enum call_status file_completion_reserve(struct native_completion *operation)
{
  struct file_object *file = (struct file_object *)operation->target.object;
  uint64_t required;
  enum npfs_operation code;
  uint64_t offset = 0;
  size_t count = 0;
  if (operation->operation == FILE_TRANSFER) {
    if (operation->input_size != sizeof(struct file_transfer_request) ||
        !operation->source.object) {
      return CALL_BAD_REQUEST;
    }
    enum call_status status = npfs_completion_reserve(operation, NPFS_READ, NULL);
    if (status != CALL_OK) {
      fat32_note_call_failure(operation->source.object);
      return status;
    }
    struct file_transfer_request request;
    memcpy(&request, operation->input, sizeof(request));
    /* The single filesystem worker owns at most one transfer scratch buffer;
     * it is not retained completion metadata. */
    status = native_completion_charge(operation, sizeof(struct file_transfer_state));
    if (status == CALL_OK) {
      status = file_transfer_job_init(operation->backend, file, operation->target.rights,
          &operation->source, &request, operation->actor);
    }
    if (status != CALL_OK) {
      fat32_note_call_failure(operation->source.object);
      npfs_completion_discard(operation);
    }
    return status;
  }
  switch (operation->operation) {
  case FILE_READ: {
    required = FILE_RIGHT_READ;
    code = NPFS_READ;
    struct file_read_request request;
    if (operation->input_size != sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    if (request.capacity > FILE_READ_MAX_BYTES) {
      return CALL_BAD_REQUEST;
    }
    offset = request.offset;
    count = request.capacity;
    break;
  }
  case FILE_WRITE: {
    required = FILE_RIGHT_WRITE;
    code = NPFS_WRITE;
    struct file_write_request request;
    if (operation->input_size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    if (request.size > FILE_WRITE_MAX_BYTES ||
        request.size != operation->input_size - sizeof(request) ||
        request.size > UINT64_MAX - request.offset) {
      return CALL_BAD_REQUEST;
    }
    offset = request.offset;
    count = request.size;
    break;
  }
  case FILE_RESIZE: {
    required = FILE_RIGHT_WRITE;
    code = NPFS_RESIZE;
    struct file_resize_request request;
    if (operation->input_size != sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, operation->input, sizeof(request));
    offset = request.size;
    break;
  }
  case FILE_SIZE:
    required = FILE_RIGHTS;
    code = NPFS_SIZE;
    if (operation->input_size) {
      return CALL_BAD_REQUEST;
    }
    break;
  default:
    return CALL_BAD_OPERATION;
  }
  if (!(operation->target.rights & required)) {
    return CALL_DENIED;
  }
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    return failure;
  }
  if (file->immutable_snapshot && (operation->operation == FILE_WRITE ||
      operation->operation == FILE_RESIZE)) {
    return CALL_READ_ONLY;
  }
  if (file->backing != FILE_RAM && file->backing != FILE_INITRD &&
      file->backing != FILE_NPFS) {
    return CALL_BAD_OPERATION;
  }
  if (file->backing == FILE_NPFS) {
    enum call_status status = npfs_completion_reserve(operation, code, file->npfs);
    if (status != CALL_OK) {
      return status;
    }
    struct npfs_job *job = operation->backend;
    job->offset = offset;
    job->count = count;
    if (code == NPFS_WRITE && count) {
      memcpy(job->data, operation->input + sizeof(struct file_write_request), count);
    }
    return CALL_OK;
  }
  lock_file(file);
  if (file->busy) {
    unlock_file(file);
    return CALL_BUSY;
  }
  file->busy = true;
  unlock_file(file);
  operation->backend = file;
  return CALL_OK;
}

void file_completion_discard(struct native_completion *operation)
{
  struct file_object *file = (struct file_object *)operation->target.object;
  if (file->backing == FILE_NPFS || operation->operation == FILE_TRANSFER) {
    npfs_completion_discard(operation);
  } else if (operation->backend) {
    KASSERT(operation->backend == file);
    operation->backend = NULL;
    file_end_operation(file);
  }
}

void file_completion_start(struct native_completion *operation)
{
  struct file_object *file = (struct file_object *)operation->target.object;
  if (file->backing == FILE_NPFS || operation->operation == FILE_TRANSFER) {
    npfs_completion_start(operation);
    return;
  }
  KASSERT(operation->backend == file);
  enum call_status status = CALL_OK;
  uint8_t output[FILE_PAYLOAD_MAX];
  size_t output_size = 0;
  operation->progress_known = operation->operation == FILE_READ ||
      operation->operation == FILE_WRITE;
  if (!native_completion_mark_started(operation)) {
    status = native_completion_cancel_reason(operation);
  } else if (file_failure_status(file) != CALL_OK) {
    status = file_failure_status(file);
  } else if (operation->operation == FILE_READ) {
    struct file_read_request request;
    memcpy(&request, operation->input, sizeof(request));
    size_t count = request.offset < file->size ? file->size - request.offset : 0;
    if (count > request.capacity) {
      count = request.capacity;
    }
    struct file_read_reply reply = {.read = count};
    memcpy(output, &reply, sizeof(reply));
    if (count && file->backing == FILE_RAM) {
      copy_out_pages(file, request.offset, count, output + sizeof(reply));
    } else if (count) {
      memcpy(output + sizeof(reply), (const uint8_t *)file->data + request.offset, count);
    }
    output_size = sizeof(reply) + count;
    operation->progress = count;
  } else if (operation->operation == FILE_WRITE) {
    struct file_write_request request;
    memcpy(&request, operation->input, sizeof(request));
    if (!request.size) {
      struct file_write_reply reply = {0};
      memcpy(output, &reply, sizeof(reply));
      output_size = sizeof(reply);
    } else if (file->backing != FILE_RAM) {
      status = CALL_READ_ONLY;
    } else {
      size_t end = request.offset + request.size;
      size_t first = request.offset / PAGE_SIZE, last = (end - 1) / PAGE_SIZE;
      size_t old_charge = file->retained_bytes;
      if (!reserve_length(file, end > file->size ? end : file->size)) {
        fail_retention(file);
        status = CALL_FILE_TOO_LARGE;
      } else if (!reserve_index(file, last + 1) || !fill_holes(file, first, last)) {
        release_length(file, old_charge);
        status = CALL_NO_MEMORY;
      } else {
        copy_in_pages(file, request.offset, request.size,
            operation->input + sizeof(request));
        if (end > file->size) {
          file->size = end;
        }
        fs_metadata_touch(&file->metadata);
        struct file_write_reply reply = {.written = request.size};
        memcpy(output, &reply, sizeof(reply));
        output_size = sizeof(reply);
        operation->progress = request.size;
      }
    }
  } else if (operation->operation == FILE_RESIZE) {
    struct file_resize_request request;
    memcpy(&request, operation->input, sizeof(request));
    if (file->backing != FILE_RAM) {
      status = CALL_READ_ONLY;
    } else {
      status = resize_accounted(file, request.size);
    }
  } else {
    KASSERT(operation->operation == FILE_SIZE);
    struct file_size_reply reply = {.size = file->size};
    memcpy(output, &reply, sizeof(reply));
    output_size = sizeof(reply);
  }
  operation->backend = NULL;
  file_end_operation(file);
  native_completion_finish(operation, status, output, output_size, NULL);
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
  if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_INFO);
    pending->job.node = file->npfs;
    pending->job.fat_node = file->fat32;
    if (file->backing == FILE_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status == CALL_OK &&
        !copy_to_user(reply_address, &pending->job.file_info, sizeof(reply))) {
      status = CALL_BAD_BUFFER;
    }
    npfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  if (file->backing == FILE_HOST) {
    struct hostfs_request *pending = hostfs_request_prepare(HOSTFS_INFO);
    pending->node = file->host;
    hostfs_request_submit_and_wait(pending);
    enum call_status status = pending->status;
    if (status == CALL_OK &&
        !copy_to_user(reply_address, &pending->info, sizeof(reply))) {
      status = CALL_BAD_BUFFER;
    }
    hostfs_request_release(pending);
    return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
  }
  if (!file_begin_operation(file)) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    file_end_operation(file);
    return (struct syscall_result){failure, 0};
  }
  if (!task_operation_start()) {
    file_end_operation(file);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  reply.valid = FILE_INFO_SIZE_VALID;
  reply.size = file->size;
  fs_metadata_info(&file->metadata, &reply);
  file_end_operation(file);
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result file_call(struct file_object *file, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if ((file->backing == FILE_NPFS || file->backing == FILE_FAT32) &&
      (rights & ~FILE_RIGHTS)) {
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
  case FILE_TRANSFER:
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
  enum call_status failure = file_failure_status(file);
  if (failure != CALL_OK) {
    return (struct syscall_result){failure, 0};
  }
  if (file->immutable_snapshot && (operation == FILE_WRITE || operation == FILE_RESIZE || operation == FILE_TRANSFER)) {
    return (struct syscall_result){CALL_READ_ONLY, 0};
  }
  if (operation == FILE_INFO) {
    return query_info(file, rights, request_size, reply_address, reply_capacity);
  }

  if (operation == FILE_TRANSFER) {
    return transfer_file(file, rights, request_address, request_size, reply_address, reply_capacity);
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
        NULL, reply_address, reply_capacity);
  }
  if (operation == FILE_SYNC) {
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_SYNC);
      pending->job.node = file->npfs;
      pending->job.fat_node = file->fat32;
      if (file->backing == FILE_FAT32) {
        pending->job.fat_actor = process_current()->control;
      }
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
    if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
      struct npfs_request *pending = npfs_request_prepare(NPFS_RESIZE);
      pending->job.node = file->npfs;
      pending->job.fat_node = file->fat32;
      if (file->backing == FILE_FAT32) {
        pending->job.fat_actor = process_current()->control;
      }
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
  if (file->backing == FILE_NPFS || file->backing == FILE_FAT32) {
    struct npfs_request *pending = npfs_request_prepare(NPFS_SIZE);
    pending->job.node = file->npfs;
    pending->job.fat_node = file->fat32;
    if (file->backing == FILE_FAT32) {
      pending->job.fat_actor = process_current()->control;
    }
    pending->job.rights = rights;
    npfs_request_submit_and_wait(pending);
    enum call_status status = pending->job.status;
    if (status != CALL_OK) {
      npfs_request_release(pending);
      return (struct syscall_result){status, 0};
    }
    reply.size = pending->job.offset;
    bool copied = copy_to_user(reply_address, &reply, sizeof(reply));
    npfs_request_release(pending);
    if (!copied) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
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
    bool copied = copy_to_user(reply_address, &reply, sizeof(reply));
    hostfs_request_release(pending);
    if (!copied) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  } else {
    if (!file_begin_operation(file)) {
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    failure = file_failure_status(file);
    if (failure != CALL_OK) {
      file_end_operation(file);
      return (struct syscall_result){failure, 0};
    }
    if (!task_operation_start()) {
      file_end_operation(file);
      return (struct syscall_result){CALL_DENIED, 0};
    }
    reply.size = file->size;
    file_end_operation(file);
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
