#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/object/pipe.h>
#include <kernel/object/terminal.h>
#include <kernel/fs/ramfs.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/capability.h>
#include <kernel/object/namespace.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/display.h>
#include <kernel/object/system_info.h>
#include <kernel/panic.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/service/request.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <stdatomic.h>

struct request_layout {
  size_t size;
  size_t alignment;
  size_t header_offset;
};

static const struct request_layout request_layouts[BSP_SERVICE_COUNT] = {
  [BSP_SERVICE_SYSTEM_INFO_MEMORY] = {sizeof(struct system_info_memory_request), alignof(struct system_info_memory_request),
      offsetof(struct system_info_memory_request, request)},
  [BSP_SERVICE_TERMINAL_CREATE] = {sizeof(struct terminal_create_service_request), alignof(struct terminal_create_service_request),
      offsetof(struct terminal_create_service_request, request)},
  [BSP_SERVICE_PIPE_CREATE] = {sizeof(struct pipe_create_request), alignof(struct pipe_create_request),
      offsetof(struct pipe_create_request, request)},
  [BSP_SERVICE_DISPLAY] = {sizeof(struct display_request), alignof(struct display_request),
      offsetof(struct display_request, request)},
  [BSP_SERVICE_CAPABILITY_GROW] = {sizeof(struct capability_growth_request), alignof(struct capability_growth_request),
      offsetof(struct capability_growth_request, request)},
  [BSP_SERVICE_NAMESPACE_CREATE] = {sizeof(struct namespace_create_request), alignof(struct namespace_create_request),
      offsetof(struct namespace_create_request, request)},
  [BSP_SERVICE_ENDPOINT_CREATE] = {sizeof(struct endpoint_create_request), alignof(struct endpoint_create_request),
      offsetof(struct endpoint_create_request, request)},
  [BSP_SERVICE_ENDPOINT_EXPORT] = {sizeof(struct endpoint_export_request), alignof(struct endpoint_export_request),
      offsetof(struct endpoint_export_request, request)},
  [BSP_SERVICE_RAMFS] = {sizeof(struct ramfs_request), alignof(struct ramfs_request),
      offsetof(struct ramfs_request, request)},
  [BSP_SERVICE_FILE_REPLACE] = {sizeof(struct file_replace_request), alignof(struct file_replace_request),
      offsetof(struct file_replace_request, request)},
  [BSP_SERVICE_LAUNCHER] = {sizeof(struct launcher_request), alignof(struct launcher_request),
      offsetof(struct launcher_request, request)},
  [BSP_SERVICE_HOSTFS] = {sizeof(struct hostfs_request), alignof(struct hostfs_request),
      offsetof(struct hostfs_request, request)},
  [BSP_SERVICE_NPFS] = {sizeof(struct npfs_request), alignof(struct npfs_request),
      offsetof(struct npfs_request, request)},
  [BSP_SERVICE_READINESS] = {sizeof(struct readiness_request), alignof(struct readiness_request),
      offsetof(struct readiness_request, request)},
};

static size_t storage_size, storage_alignment;
static struct bsp_request *request_head, *request_tail;
static atomic_bool requests_locked;
static struct task_wait *worker_wait; /* requests_locked; detached before notification. */
static bool initialized; /* Published to APs by scheduler startup. */

/* IF=0. Never enter the scheduler or a subsystem while holding this lock. */
static void lock_requests(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&requests_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_requests(void)
{
  atomic_store_explicit(&requests_locked, false, memory_order_release);
}

struct bsp_request *bsp_request_storage_create(void)
{
  KASSERT(arch_cpu_index() == 0 && initialized);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct bsp_request *storage = kmalloc(storage_size);
  if (storage) {
    KASSERT((uintptr_t)storage % storage_alignment == 0);
    *storage = (struct bsp_request){0};
  }
  return storage;
}

void bsp_request_storage_destroy(struct bsp_request *storage)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (storage) {
    KASSERT(storage->state == BSP_REQUEST_FREE && !storage->next && !storage->wait);
    kfree(storage);
  }
}

struct bsp_request *bsp_request_prepare(enum bsp_service service)
{
  KASSERT(initialized && (unsigned)service < BSP_SERVICE_COUNT);
  const struct request_layout *layout = &request_layouts[service];
  KASSERT(layout->size <= storage_size && layout->alignment <= storage_alignment);
  struct bsp_request *request = task_bsp_request_acquire();
  KASSERT(request->state == BSP_REQUEST_FREE);
  KASSERT((uintptr_t)request % layout->alignment == 0);
  memset(request, 0, layout->size);
  *request = (struct bsp_request){
    .wait = task_wait_prepare(),
    .cleanup_group = task_cleanup_group(),
    .service = service,
    .state = BSP_REQUEST_PREPARED,
  };
  return request;
}

static bool requires_handoff(enum bsp_service service)
{
  switch (service) {
  case BSP_SERVICE_TERMINAL_CREATE:
  case BSP_SERVICE_PIPE_CREATE:
  case BSP_SERVICE_CAPABILITY_GROW:
  case BSP_SERVICE_NAMESPACE_CREATE:
  case BSP_SERVICE_ENDPOINT_CREATE:
  case BSP_SERVICE_ENDPOINT_EXPORT:
  case BSP_SERVICE_RAMFS:
  case BSP_SERVICE_FILE_REPLACE:
  case BSP_SERVICE_LAUNCHER:
  case BSP_SERVICE_HOSTFS:
  case BSP_SERVICE_NPFS:
  case BSP_SERVICE_READINESS:
  case BSP_SERVICE_SYSTEM_INFO_MEMORY:
    return false;
  case BSP_SERVICE_DISPLAY:
    return true;
  default:
    panic("unknown BSP service %u", (unsigned)service);
  }
}

static void publish_request(struct bsp_request *request)
{
  if (request->service == BSP_SERVICE_FILE_REPLACE) {
    file_replace_published((struct file_replace_request *)request);
  } else if (request->service == BSP_SERVICE_HOSTFS) {
    hostfs_request_published((struct hostfs_request *)request);
  } else if (request->service == BSP_SERVICE_NPFS) {
    npfs_request_published((struct npfs_request *)request);
  }
  lock_requests();
  request->state = BSP_REQUEST_QUEUED;
  if (request_tail) {
    request_tail->next = request;
  } else {
    request_head = request;
  }
  request_tail = request;
  struct task_wait *wake = worker_wait;
  worker_wait = NULL;
  unlock_requests();

  /* Only the first publisher takes the idle worker's waiter. A running or
   * already notified worker needs no additional notification. */
  if (wake) {
    task_wait_wake(wake);
  }
}

void bsp_request_publish_deferred(struct bsp_request *request)
{
  KASSERT(request && request->state == BSP_REQUEST_DEFERRED);
  KASSERT(requires_handoff(request->service));
  publish_request(request);
}

void bsp_request_submit_and_wait(struct bsp_request *request)
{
  KASSERT(initialized && request && request->state == BSP_REQUEST_PREPARED);
  KASSERT(request == task_bsp_request_current());
  struct task_wait *wait = request->wait;
  KASSERT(wait && !request->next);

  if (requires_handoff(request->service)) {
    request->state = BSP_REQUEST_DEFERRED;
    task_bsp_request_defer(request);
  } else {
    publish_request(request);
  }
  /* Only the saved wait is accessible until notification. Deferred requests
   * cannot complete early: publication requires the scheduler's safe handoff. */
  task_wait_sleep(wait);
  KASSERT(request->state == BSP_REQUEST_COMPLETE && !request->wait && !request->next);
}

void bsp_request_release(struct bsp_request *request)
{
  KASSERT(request && request->state == BSP_REQUEST_COMPLETE);
  KASSERT(!request->next && !request->wait);
  task_bsp_request_release(request);
  *request = (struct bsp_request){0};
}

void bsp_request_complete(struct bsp_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->state == BSP_REQUEST_SERVICING ||
      ((request->service == BSP_SERVICE_HOSTFS || request->service == BSP_SERVICE_NPFS ||
        request->service == BSP_SERVICE_READINESS) &&
       request->state == BSP_REQUEST_FORWARDED));
  KASSERT(!request->next && request->wait);
  struct task_wait *wait = request->wait;
  request->wait = NULL;
  request->cleanup_group = NULL;
  request->state = BSP_REQUEST_COMPLETE;
  /* Results and completion precede notification. The caller may consume,
   * reuse or retire storage as soon as wake publishes it; no accesses follow. */
  task_wait_wake(wait);
}

static void service_request(struct bsp_request *request)
{
  struct execution_group *previous = object_cleanup_enter(request->cleanup_group);
  switch (request->service) {
  case BSP_SERVICE_READINESS:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    readiness_submit((struct readiness_request *)request);
    return;
  case BSP_SERVICE_HOSTFS:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    hostfs_submit((struct hostfs_request *)request);
    /* Forwarding can complete immediately. The worker owns final completion;
     * even inspecting the request after this transfer would race its caller. */
    return;
  case BSP_SERVICE_NPFS:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    npfs_forward((struct npfs_request *)request);
    return;
  case BSP_SERVICE_TERMINAL_CREATE:
    terminal_create_execute((struct terminal_create_service_request *)request);
    break;
  case BSP_SERVICE_PIPE_CREATE:
    pipe_create_execute((struct pipe_create_request *)request);
    break;
  case BSP_SERVICE_CAPABILITY_GROW:
    capability_growth_execute((struct capability_growth_request *)request);
    break;
  case BSP_SERVICE_NAMESPACE_CREATE:
    namespace_create_execute((struct namespace_create_request *)request);
    break;
  case BSP_SERVICE_ENDPOINT_CREATE:
    endpoint_create_execute((struct endpoint_create_request *)request);
    break;
  case BSP_SERVICE_ENDPOINT_EXPORT:
    endpoint_export_execute((struct endpoint_export_request *)request);
    break;
  case BSP_SERVICE_RAMFS:
    ramfs_request_execute((struct ramfs_request *)request);
    break;
  case BSP_SERVICE_FILE_REPLACE:
    file_replace_execute((struct file_replace_request *)request);
    break;
  case BSP_SERVICE_LAUNCHER:
    launcher_request_execute((struct launcher_request *)request);
    break;
  case BSP_SERVICE_SYSTEM_INFO_MEMORY:
    system_info_memory_execute((struct system_info_memory_request *)request);
    break;
    break;
  case BSP_SERVICE_DISPLAY:
    display_request_execute((struct display_request *)request);
    break;
  default:
    KASSERT(false);
  }
  object_cleanup_leave(previous);
  bsp_request_complete(request);
}

static void request_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
    lock_requests();
    struct bsp_request *request = request_head;
    if (!request) {
      struct task_wait *wait = task_wait_prepare();
      KASSERT(!worker_wait);
      worker_wait = wait;
      unlock_requests();
      /* Always finish this wait: a publisher may already hold its pointer.
       * Queue inspection must not bypass notification and reuse the record. */
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }

    request_head = request->next;
    if (!request_head) {
      request_tail = NULL;
    }
    KASSERT(request->state == BSP_REQUEST_QUEUED);
    request->next = NULL;
    request->state = BSP_REQUEST_SERVICING;
    unlock_requests();

    service_request(request);
    cpu_restore_interrupts(flags);
    kernel_task_yield_if_runnable();
  }
}

void bsp_requests_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !initialized);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  for (size_t i = 0; i < BSP_SERVICE_COUNT; ++i) {
    const struct request_layout *layout = &request_layouts[i];
    KASSERT(layout->size >= sizeof(struct bsp_request) && !layout->header_offset);
    KASSERT(layout->alignment && layout->alignment <= alignof(max_align_t));
    if (layout->size > storage_size) {
      storage_size = layout->size;
    }
    if (layout->alignment > storage_alignment) {
      storage_alignment = layout->alignment;
    }
  }
  enum mm_result result = kernel_task_create(request_worker, NULL);
  if (result != MM_OK) {
    panic("cannot create BSP request executor (error %u)", (unsigned)result);
  }
  readiness_init();
  initialized = true;
}
