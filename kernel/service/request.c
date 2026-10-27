#include <kernel/object/wifi.h>
#include <kernel/object/wifi_connection.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/object/screenshot_saver.h>
#include <kernel/object/clipboard.h>
#include <kernel/completion.h>
#include <kernel/object/net_factory.h>
#include <kernel/object/net_config.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/object/pipe.h>
#include <kernel/object/output.h>
#include <kernel/object/retention.h>
#include <kernel/object/terminal.h>
#include <kernel/fs/ramfs.h>
#include <kernel/fs/hostfs.h>
#include <kernel/fs/npfs.h>
#include <kernel/acpi.h>
#include <kernel/acpi/tables.h>
#include <kernel/object/file.h>
#include <kernel/object/launcher.h>
#include <kernel/object/capability.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/environment.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/display.h>
#include <kernel/object/memory.h>
#include <kernel/object/jit.h>
#include <kernel/object/thread.h>
#include <kernel/object/process.h>
#include <kernel/process_vm.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/audio.h>
#include <kernel/object/bluetooth_hci.h>
#include <kernel/object/screen_capture.h>
#include <kernel/display_capture.h>
#include <kernel/object/system_info.h>
#include <kernel/object/input_policy.h>
#include <kernel/panic.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/service/request.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user/readiness_set.h>
#include <stdatomic.h>

struct request_layout {
  size_t size;
  size_t alignment;
  size_t header_offset;
};

static const struct request_layout request_layouts[BSP_SERVICE_COUNT] = {
  [BSP_SERVICE_WIFI_AUTH] = {sizeof(struct wifi_auth_request), alignof(struct wifi_auth_request),
    offsetof(struct wifi_auth_request, request)},
  [BSP_SERVICE_NET_CONTROL] = {sizeof(struct net_control_service_request),
    alignof(struct net_control_service_request),
    offsetof(struct net_control_service_request, request)},
  [BSP_SERVICE_NET_FACTORY] = {sizeof(struct net_factory_derive_service_request),
    alignof(struct net_factory_derive_service_request),
    offsetof(struct net_factory_derive_service_request, request)},
  [BSP_SERVICE_WIFI_CONNECTION] = {sizeof(struct wifi_connection_request),
    alignof(struct wifi_connection_request), offsetof(struct wifi_connection_request, request)},
  [BSP_SERVICE_WIFI] = {sizeof(struct wifi_request), alignof(struct wifi_request),
    offsetof(struct wifi_request, request)},
  [BSP_SERVICE_RETENTION] = {sizeof(struct retention_request), alignof(struct retention_request),
    offsetof(struct retention_request, request)},
  [BSP_SERVICE_OUTPUT] = {sizeof(struct output_request), alignof(struct output_request),
    offsetof(struct output_request, request)},
  [BSP_SERVICE_FORWARDER] = {sizeof(struct forwarder_request), alignof(struct forwarder_request),
    offsetof(struct forwarder_request, request)},
  [BSP_SERVICE_COMPLETION] = {sizeof(struct completion_admit_request), alignof(struct completion_admit_request),
    offsetof(struct completion_admit_request, request)},
  [BSP_SERVICE_THREAD] = {sizeof(struct thread_request), alignof(struct thread_request),
    offsetof(struct thread_request, request)},
  [BSP_SERVICE_PROCESS_INTERRUPT_CREATE] = {sizeof(struct process_interrupt_create_request),
    alignof(struct process_interrupt_create_request),
    offsetof(struct process_interrupt_create_request, request)},
  [BSP_SERVICE_CLIPBOARD] = {sizeof(struct clipboard_request), alignof(struct clipboard_request),
      offsetof(struct clipboard_request, request)},
  [BSP_SERVICE_AUDIO] = {sizeof(struct audio_request), alignof(struct audio_request),
      offsetof(struct audio_request, request)},
  [BSP_SERVICE_BLUETOOTH_HCI] = {sizeof(struct bluetooth_hci_request), alignof(struct bluetooth_hci_request),
      offsetof(struct bluetooth_hci_request, request)},
  [BSP_SERVICE_SCREENSHOT_SAVER] = {sizeof(struct screenshot_saver_request), alignof(struct screenshot_saver_request),
      offsetof(struct screenshot_saver_request, request)},
  [BSP_SERVICE_SCREEN_CAPTURE] = {sizeof(struct screen_capture_request), alignof(struct screen_capture_request),
      offsetof(struct screen_capture_request, request)},
  [BSP_SERVICE_POWER] = {sizeof(struct acpi_power_request), alignof(struct acpi_power_request),
      offsetof(struct acpi_power_request, request)},
  [BSP_SERVICE_ACPI_TABLES] = {sizeof(struct acpi_tables_request), alignof(struct acpi_tables_request),
      offsetof(struct acpi_tables_request, request)},
  [BSP_SERVICE_SYSTEM_INFO_MEMORY] = {sizeof(struct system_info_memory_request), alignof(struct system_info_memory_request),
      offsetof(struct system_info_memory_request, request)},
  [BSP_SERVICE_SYSTEM_INFO_POWER] = {sizeof(struct system_info_power_request), alignof(struct system_info_power_request),
      offsetof(struct system_info_power_request, request)},
  [BSP_SERVICE_INPUT_POLICY] = {sizeof(struct input_policy_install_request), alignof(struct input_policy_install_request),
      offsetof(struct input_policy_install_request, request)},
  [BSP_SERVICE_SYSTEM_INFO_HOSTNAME] = {sizeof(struct system_info_hostname_request), alignof(struct system_info_hostname_request),
      offsetof(struct system_info_hostname_request, request)},
  [BSP_SERVICE_TERMINAL_CREATE] = {sizeof(struct terminal_create_service_request), alignof(struct terminal_create_service_request),
      offsetof(struct terminal_create_service_request, request)},
  [BSP_SERVICE_PIPE_CREATE] = {sizeof(struct pipe_create_request), alignof(struct pipe_create_request),
      offsetof(struct pipe_create_request, request)},
  [BSP_SERVICE_POINTER] = {sizeof(struct pointer_request), alignof(struct pointer_request),
      offsetof(struct pointer_request, request)},
  [BSP_SERVICE_DISPLAY] = {sizeof(struct display_request), alignof(struct display_request),
      offsetof(struct display_request, request)},
  [BSP_SERVICE_MEMORY] = {sizeof(struct memory_request), alignof(struct memory_request),
      offsetof(struct memory_request, request)},
  [BSP_SERVICE_JIT] = {sizeof(struct jit_request), alignof(struct jit_request),
      offsetof(struct jit_request, request)},
  [BSP_SERVICE_CAPABILITY_GROW] = {sizeof(struct capability_growth_request), alignof(struct capability_growth_request),
      offsetof(struct capability_growth_request, request)},
  [BSP_SERVICE_ENVIRONMENT] = {sizeof(struct environment_request), alignof(struct environment_request),
      offsetof(struct environment_request, request)},
  [BSP_SERVICE_ENDPOINT_CREATE] = {sizeof(struct endpoint_create_request), alignof(struct endpoint_create_request),
      offsetof(struct endpoint_create_request, request)},
  [BSP_SERVICE_ENDPOINT_EXPORT] = {sizeof(struct endpoint_export_request), alignof(struct endpoint_export_request),
      offsetof(struct endpoint_export_request, request)},
  [BSP_SERVICE_ENDPOINT_DESCRIBE] = {sizeof(struct endpoint_describe_request), alignof(struct endpoint_describe_request),
      offsetof(struct endpoint_describe_request, request)},
  [BSP_SERVICE_RAMFS] = {sizeof(struct ramfs_request), alignof(struct ramfs_request),
      offsetof(struct ramfs_request, request)},
  [BSP_SERVICE_LAUNCHER] = {sizeof(struct launcher_request), alignof(struct launcher_request),
      offsetof(struct launcher_request, request)},
  [BSP_SERVICE_HOSTFS] = {sizeof(struct hostfs_request), alignof(struct hostfs_request),
      offsetof(struct hostfs_request, request)},
  [BSP_SERVICE_NPFS] = {sizeof(struct npfs_request), alignof(struct npfs_request),
      offsetof(struct npfs_request, request)},
  [BSP_SERVICE_READINESS_SET] = {sizeof(struct readiness_set_request), alignof(struct readiness_set_request),
      offsetof(struct readiness_set_request, request)},
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
    .operation = task_forwarder_operation(),
    .service = service,
    .state = BSP_REQUEST_PREPARED,
  };
  return request;
}

static bool requires_handoff(enum bsp_service service)
{
  switch (service) {
  case BSP_SERVICE_FORWARDER:
  case BSP_SERVICE_COMPLETION:
  case BSP_SERVICE_NET_CONTROL:
  case BSP_SERVICE_NET_FACTORY:
  case BSP_SERVICE_WIFI:
  case BSP_SERVICE_WIFI_CONNECTION:
  case BSP_SERVICE_WIFI_AUTH:
  case BSP_SERVICE_TERMINAL_CREATE:
  case BSP_SERVICE_RETENTION:
  case BSP_SERVICE_OUTPUT:
  case BSP_SERVICE_PIPE_CREATE:
  case BSP_SERVICE_CAPABILITY_GROW:
  case BSP_SERVICE_ENVIRONMENT:
  case BSP_SERVICE_ENDPOINT_CREATE:
  case BSP_SERVICE_ENDPOINT_EXPORT:
  case BSP_SERVICE_ENDPOINT_DESCRIBE:
  case BSP_SERVICE_RAMFS:
  case BSP_SERVICE_LAUNCHER:
  case BSP_SERVICE_HOSTFS:
  case BSP_SERVICE_NPFS:
  case BSP_SERVICE_READINESS:
  case BSP_SERVICE_READINESS_SET:
  case BSP_SERVICE_PROCESS_INTERRUPT_CREATE:
  case BSP_SERVICE_SYSTEM_INFO_MEMORY:
  case BSP_SERVICE_SYSTEM_INFO_POWER:
  case BSP_SERVICE_SYSTEM_INFO_HOSTNAME:
  case BSP_SERVICE_INPUT_POLICY:
  case BSP_SERVICE_SCREENSHOT_SAVER:
  case BSP_SERVICE_POWER:
  case BSP_SERVICE_ACPI_TABLES:
  case BSP_SERVICE_AUDIO:
  case BSP_SERVICE_CLIPBOARD:
  case BSP_SERVICE_BLUETOOTH_HCI:
    return false;
  case BSP_SERVICE_POINTER:
  case BSP_SERVICE_DISPLAY:
  case BSP_SERVICE_SCREEN_CAPTURE:
  case BSP_SERVICE_THREAD:
    return true;
  default:
    panic("unknown BSP service %u", (unsigned)service);
  }
}

static void publish_request(struct bsp_request *request)
{
  if (request->service == BSP_SERVICE_HOSTFS) {
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
  KASSERT(request->service != BSP_SERVICE_MEMORY && request->service != BSP_SERVICE_JIT);
  /* Both typed local services execute on their retained caller CPU. */
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

void bsp_request_cancel_prepared(struct bsp_request *request)
{
  KASSERT(request && request == task_bsp_request_current());
  KASSERT(request->state == BSP_REQUEST_PREPARED && !request->next &&
      !request->vm_process && !request->vm_mutator && !request->vm_reply_owned);
  request->wait = NULL;
  request->cleanup_group = NULL;
  task_bsp_request_release(request);
  *request = (struct bsp_request){0};
}

void bsp_request_complete(struct bsp_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->state == BSP_REQUEST_SERVICING ||
      ((request->service == BSP_SERVICE_HOSTFS || request->service == BSP_SERVICE_NPFS ||
        request->service == BSP_SERVICE_READINESS || request->service == BSP_SERVICE_READINESS_SET ||
        request->service == BSP_SERVICE_POWER ||
        request->service == BSP_SERVICE_ACPI_TABLES ||
        request->service == BSP_SERVICE_SCREEN_CAPTURE || request->service == BSP_SERVICE_AUDIO ||
        request->service == BSP_SERVICE_BLUETOOTH_HCI || request->service == BSP_SERVICE_WIFI_AUTH ||
        request->service == BSP_SERVICE_DISPLAY ||
        request->service == BSP_SERVICE_THREAD) &&
       request->state == BSP_REQUEST_FORWARDED));
  KASSERT(!request->next && request->wait);
  KASSERT(!request->vm_process && !request->vm_mutator);
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
  case BSP_SERVICE_PROCESS_INTERRUPT_CREATE:
    process_interrupt_create_execute((struct process_interrupt_create_request *)request);
    break;
  case BSP_SERVICE_WIFI_AUTH:
    wifi_auth_execute((struct wifi_auth_request *)request);
    if (request->state == BSP_REQUEST_FORWARDED) {
      object_cleanup_leave(previous);
      return;
    }
    break;
  case BSP_SERVICE_SCREENSHOT_SAVER:
    screenshot_saver_execute((struct screenshot_saver_request *)request);
    break;
  case BSP_SERVICE_FORWARDER:
    forwarder_execute((struct forwarder_request *)request);
    break;
  case BSP_SERVICE_BLUETOOTH_HCI:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    bluetooth_hci_request_forward((struct bluetooth_hci_request *)request);
    return;
  case BSP_SERVICE_SCREEN_CAPTURE:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    screen_capture_submit((struct screen_capture_request *)request);
    return;
  case BSP_SERVICE_AUDIO:
    object_cleanup_leave(previous);
    audio_request_forward((struct audio_request *)request);
    return;
  case BSP_SERVICE_READINESS_SET:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    readiness_set_submit((struct readiness_set_request *)request);
    return;
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
  case BSP_SERVICE_POWER:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    acpi_power_forward((struct acpi_power_request *)request);
    return;
  case BSP_SERVICE_ACPI_TABLES:
    object_cleanup_leave(previous);
    request->state = BSP_REQUEST_FORWARDED;
    acpi_tables_forward((struct acpi_tables_request *)request);
    return;
  case BSP_SERVICE_TERMINAL_CREATE:
    terminal_create_execute((struct terminal_create_service_request *)request);
    break;
  case BSP_SERVICE_NET_CONTROL:
    net_control_execute((struct net_control_service_request *)request);
    break;
  case BSP_SERVICE_NET_FACTORY:
    net_factory_derive_execute((struct net_factory_derive_service_request *)request);
    break;
  case BSP_SERVICE_WIFI_CONNECTION:
    wifi_connection_execute((struct wifi_connection_request *)request);
    break;
  case BSP_SERVICE_WIFI:
    wifi_request_execute((struct wifi_request *)request);
    break;
  case BSP_SERVICE_RETENTION:
    retention_execute((struct retention_request *)request);
    break;
  case BSP_SERVICE_OUTPUT:
    output_execute((struct output_request *)request);
    break;
  case BSP_SERVICE_PIPE_CREATE:
    pipe_create_execute((struct pipe_create_request *)request);
    break;
  case BSP_SERVICE_CAPABILITY_GROW:
    capability_growth_execute((struct capability_growth_request *)request);
    break;
  case BSP_SERVICE_ENVIRONMENT:
    environment_execute((struct environment_request *)request);
    break;
  case BSP_SERVICE_COMPLETION:
    completion_admit_execute((struct completion_admit_request *)request);
    break;
  case BSP_SERVICE_ENDPOINT_CREATE:
    endpoint_create_execute((struct endpoint_create_request *)request);
    break;
  case BSP_SERVICE_ENDPOINT_EXPORT:
    endpoint_export_execute((struct endpoint_export_request *)request);
    break;
  case BSP_SERVICE_ENDPOINT_DESCRIBE:
    endpoint_describe_execute((struct endpoint_describe_request *)request);
    break;
  case BSP_SERVICE_RAMFS:
    ramfs_request_execute((struct ramfs_request *)request);
    break;
  case BSP_SERVICE_LAUNCHER:
    launcher_request_execute((struct launcher_request *)request);
    break;
  case BSP_SERVICE_SYSTEM_INFO_MEMORY:
    system_info_memory_execute((struct system_info_memory_request *)request);
    break;
  case BSP_SERVICE_SYSTEM_INFO_POWER:
    system_info_power_execute((struct system_info_power_request *)request);
    break;
  case BSP_SERVICE_INPUT_POLICY:
    input_policy_execute((struct input_policy_install_request *)request);
    break;
  case BSP_SERVICE_SYSTEM_INFO_HOSTNAME:
    system_info_hostname_execute((struct system_info_hostname_request *)request);
    break;
  case BSP_SERVICE_CLIPBOARD:
    clipboard_request_execute((struct clipboard_request *)request);
    break;
  case BSP_SERVICE_POINTER:
    pointer_request_execute((struct pointer_request *)request);
    break;
  case BSP_SERVICE_DISPLAY:
    if (request->vm_process) {
      object_cleanup_leave(previous);
      process_vm_mutation_forward(request);
      return;
    }
    display_request_execute((struct display_request *)request);
    break;
  case BSP_SERVICE_THREAD:
    object_cleanup_leave(previous);
    process_vm_mutation_forward(request);
    return;
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

    struct forwarder_operation *previous = task_forwarder_set(request->operation);
    service_request(request);
    task_forwarder_set(previous);
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
  clipboard_init();
  process_vm_init();
  initialized = true;
}
