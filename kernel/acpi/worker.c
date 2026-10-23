#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/io_apic.h>
#include <arch/smp.h>
#include <kernel/acpi.h>
#include <kernel/acpi/pci_resources.h>
#include <kernel/acpi/tables.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <uacpi/event.h>
#include <uacpi/kernel_api.h>
#include <uacpi/namespace.h>
#include <uacpi/notify.h>
#include <uacpi/uacpi.h>
#include "host.h"

#define NANOSECONDS_PER_MICROSECOND 1000
/* An unclaimed level-triggered SCI would fire again at once; it stays masked
 * this long before the worker tries again. */
#define SCI_REARM_NS UINT64_C(1000000000)
/* Deferred work items, and separately table requests, run per top-level turn.
 * Work that keeps queueing more, such as firmware raising an SCI on every EC
 * query or processes repeatedly reading acpi://, cannot then keep the worker
 * from its power requests, button press and device polls. */
#define WORK_PER_TURN 8u
/* Before the power button is armed, deferred work and SCIs are drained until
 * quiet for at most this long, so a boot-latched or in-operation event cannot
 * count as a press once armed. A source still busy at the bound is armed over. */
#define QUIESCE_NS UINT64_C(1000000000)

struct acpi_work {
  struct acpi_work *next;
  uacpi_work_handler handler;
  uacpi_handle context;
};

/* Shared with SCI interrupt entry and the BSP request executor. The SCI is
 * routed to the BSP and both workers are pinned there, so IF=0 on the BSP
 * serializes them without a lock. The power request stays here while it runs. */
static struct task_wait *worker_wait;
static bool sci_pending, sci_shared_claimed;
static bool ready;
static struct acpi_power_request *power_request;
static bool power_started;
/* A power operation started by the power button rather than a request. */
static bool power_running;

/* Worker only. The registration's address is uACPI's opaque SCI handle. */
static struct sci_registration {
  uacpi_interrupt_handler handler;
  uacpi_handle context;
} sci;
static struct acpi_work *work_head, *work_tail;
static size_t work_queued;
static bool running_work;
static uint64_t sci_rearm_at;
static size_t unclaimed_scis;
/* Fixed events and deferred Notify delivery share this worker-owned latch. */
static bool button_pressed, button_armed, quiesce_logged;
/* A local power operation from acpi_power_local(), pending until it fails. */
static bool local_pending, local_failed;
static bool local_force, local_force_allowed;
static enum acpi_power_action local_action;
static enum call_status local_status;
static char worker_identity;

/* Bootstrap only, then read by the worker. */
static uint64_t rsdp_physical;

static void acpi_worker(void *argument);

void acpi_prepare(const struct boot_info *boot)
{
  if (!boot->acpi_rsdp) {
    acpi_wifi_unavailable();
    kerror("ACPI: no RSDP; ACPI unavailable\n");
    return;
  }
  if (acpi_map_prepare(boot)) {
    rsdp_physical = boot->acpi_rsdp;
  } else {
    acpi_wifi_unavailable();
  }
}

void acpi_start(void)
{
  if (!rsdp_physical) {
    return;
  }
  enum mm_result result = kernel_task_create(acpi_worker, NULL);
  if (result != MM_OK) {
    acpi_wifi_unavailable();
    kerror("ACPI: cannot create worker (error %u); ACPI unavailable\n", (unsigned)result);
  }
}

void acpi_require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(kernel_task_is_current(acpi_worker, NULL));
  cpu_restore_interrupts(flags);
}

/* BSP, IF=0. */
static void wake_worker(void)
{
  struct task_wait *wait = worker_wait;
  worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void acpi_worker_notify(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  wake_worker();
}

void acpi_power_forward(struct acpi_power_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!ready || power_request || power_running || local_pending) {
    request->status = ready ? CALL_BUSY : CALL_UNAVAILABLE;
    bsp_request_complete(&request->request);
    return;
  }
  power_request = request;
  power_started = false;
  wake_worker();
}

static enum call_status queue_local_power(enum acpi_power_action action, bool force)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!ready) {
    return CALL_UNAVAILABLE;
  }
  if (power_request || power_running || local_pending) {
    return CALL_BUSY;
  }
  if (force && !local_force_allowed) {
    return CALL_DENIED;
  }
  local_pending = true;
  local_failed = false;
  local_force = force;
  local_force_allowed = false;
  local_action = action;
  wake_worker();
  return CALL_OK;
}

enum call_status acpi_power_local(enum acpi_power_action action)
{
  return queue_local_power(action, false);
}

enum call_status acpi_power_local_force(enum acpi_power_action action)
{
  return queue_local_power(action, true);
}

bool acpi_power_local_failed(enum call_status *status)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!local_failed) {
    return false;
  }
  local_failed = false;
  local_force_allowed = true;
  *status = local_status;
  return true;
}

void acpi_interrupt(bool shared_claimed)
{
  KASSERT(arch_cpu_index() == 0);
  io_apic_sci_mask();
  sci_pending = true;
  sci_shared_claimed |= shared_claimed;
  wake_worker();
}

/* Runs uACPI's handler with IF=0, as interrupt entry would. An unclaimed SCI
 * stays masked until the top level re-arms it. */
static void service_sci(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (sci_pending) {
    sci_pending = false;
    bool shared_claimed = sci_shared_claimed;
    sci_shared_claimed = false;
    if (sci.handler) {
      if (sci.handler(sci.context) == UACPI_INTERRUPT_HANDLED || shared_claimed) {
        io_apic_sci_unmask();
      } else {
        if (unclaimed_scis++ == 0) {
          kerror("ACPI: error: unclaimed SCI; ACPI events paused for 1 s at a time\n");
        } else {
          ktrace("ACPI: unclaimed SCI %zu\n", unclaimed_scis);
        }
        sci_rearm_at = arch_monotonic_ns() + SCI_REARM_NS;
      }
    }
  }
  cpu_restore_interrupts(flags);
}

/* Top level. Returns when the masked SCI is next due, or UINT64_MAX. */
static uint64_t rearm_sci(uint64_t now)
{
  uint64_t flags = cpu_save_interrupts();
  if (sci_rearm_at && now >= sci_rearm_at) {
    sci_rearm_at = 0;
    if (sci.handler) {
      io_apic_sci_unmask();
    }
  }
  uint64_t due = sci_rearm_at ? sci_rearm_at : UINT64_MAX;
  cpu_restore_interrupts(flags);
  return due;
}

/* IF=1. Sleep until an SCI arrives, deferred work is queued when WORK_WAKES,
 * or DEADLINE passes. */
static void sleep_for_events(uint64_t deadline, bool work_wakes)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  bool power = (power_request && !power_started) || button_pressed ||
      (local_pending && !power_running);
  if (!sci_pending && !(work_wakes && (work_head || power || acpi_tables_pending()))) {
    struct task_wait *wait = task_wait_prepare();
    worker_wait = wait;
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    worker_wait = NULL;
  }
  cpu_restore_interrupts(flags);
}

void acpi_worker_block(uint64_t deadline)
{
  sleep_for_events(deadline, false);
  service_sci();
}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,
                                                    uacpi_interrupt_handler handler,
                                                    uacpi_handle context,
                                                    uacpi_handle *registration)
{
  acpi_require_worker();
  unsigned sci_irq;
  if (!io_apic_sci_available(&sci_irq) || irq != sci_irq) {
    kerror("ACPI: interrupt %u is not the routed SCI; handler not installed\n", irq);
    return UACPI_STATUS_UNIMPLEMENTED;
  }
  if (sci.handler) {
    return UACPI_STATUS_ALREADY_EXISTS;
  }

  uint64_t flags = cpu_save_interrupts();
  sci = (struct sci_registration){.handler = handler, .context = context};
  io_apic_sci_unmask();
  cpu_restore_interrupts(flags);
  *registration = &sci;
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                                      uacpi_handle registration)
{
  acpi_require_worker();
  if (registration != &sci || handler != sci.handler) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }

  uint64_t flags = cpu_save_interrupts();
  io_apic_sci_disable();
  sci = (struct sci_registration){0};
  sci_pending = false;
  sci_shared_claimed = false;
  sci_rearm_at = 0;
  cpu_restore_interrupts(flags);
  return UACPI_STATUS_OK;
}

/* Both kinds of work run in order on the BSP worker, which also meets GPE
 * work's requirement to run on CPU 0. */
uacpi_handle uacpi_kernel_create_work_item(void)
{
  struct acpi_work *work = uacpi_kernel_alloc(sizeof(*work));
  if (work) {
    *work = (struct acpi_work){0};
  }
  return work;
}

void uacpi_kernel_free_work_item(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct acpi_work));
}

void uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_handle item,
                                uacpi_work_handler handler, uacpi_handle context)
{
  (void)type;
  acpi_require_worker();
  struct acpi_work *work = item;
  *work = (struct acpi_work){.handler = handler, .context = context};
  ++work_queued;
  if (work_tail) {
    work_tail->next = work;
  } else {
    work_head = work;
  }
  work_tail = work;
}

/* Runs queued work in order, at most LIMIT items. Work queued while it runs,
 * for example by a serviced SCI, can wait for a later call. */
static void run_work(size_t limit)
{
  for (size_t done = 0; work_head && done < limit; ++done) {
    service_sci();
    struct acpi_work *work = work_head;
    --work_queued;
    work_head = work->next;
    if (!work_head) {
      work_tail = NULL;
    }
    /* The handler may reschedule or free its item, so take what it needs first. */
    uacpi_work_handler handler = work->handler;
    uacpi_handle context = work->context;
    running_work = true;
    handler(context);
    running_work = false;
  }
}

/* Nothing runs concurrently with the worker, so in-flight interrupts and work
 * are the pending SCI and the queued work, completed here in that order. This
 * uACPI version also waits for work that the waited-for work queues. */
uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
  acpi_require_worker();
  if (running_work) {
    kerror("ACPI: error: deferred work cannot wait for deferred work\n");
    return UACPI_STATUS_INTERNAL_ERROR;
  }
  service_sci();
  while (work_head) {
    run_work(work_queued);
  }
  return UACPI_STATUS_OK;
}

uacpi_thread_id uacpi_kernel_get_thread_id(void)
{
  acpi_require_worker();
  return &worker_identity;
}

uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *address)
{
  *address = rsdp_physical;
  return UACPI_STATUS_OK;
}

/* Loads tables, enters ACPI mode, loads the namespace (installing the SCI
 * handler), attaches an ECDT embedded controller and runs _STA/_INI. GPEs and
 * fixed events stay disabled. */
static bool load_namespace(void)
{
  uint64_t start = arch_monotonic_ns();
  const char *step = "initialization";
  uacpi_status status = uacpi_initialize(0);
  if (status == UACPI_STATUS_OK) {
    acpi_tables_available();
    step = "namespace load";
    status = uacpi_namespace_load();
  }
  if (status == UACPI_STATUS_OK) {
    acpi_ec_load_ecdt();
    acpi_cmos_start();
    step = "namespace initialization";
    status = uacpi_namespace_initialize();
  }
  if (status != UACPI_STATUS_OK) {
    kerror("ACPI: error: %s failed: %s; ACPI unavailable\n", step,
           uacpi_status_to_string(status));
    return false;
  }

  uint64_t elapsed = arch_monotonic_ns() - start;
  struct acpi_heap_use heap = acpi_heap_use();
  kinfo("ACPI: uACPI %u.%u.%u namespace ready in %llu us; heap %zu bytes in %zu blocks, "
        "firmware window %zu pages\n", UACPI_MAJOR, UACPI_MINOR, UACPI_PATCH,
        (unsigned long long)(elapsed / NANOSECONDS_PER_MICROSECOND), heap.bytes,
        heap.blocks, acpi_map_pages());
  return true;
}

/* Root tracing also sees notifications with a device-specific handler. */
static uacpi_status trace_notify(uacpi_handle context, uacpi_namespace_node *node,
                                 uacpi_u64 value)
{
  (void)context;
  const char *path = uacpi_namespace_node_generate_absolute_path(node);
  ktrace("ACPI: Notify(%s, 0x%llx)\n", path ? path : "?", (unsigned long long)value);
  if (path) {
    uacpi_free_absolute_path(path);
  }
  return UACPI_STATUS_OK;
}

/* Top level, button disarmed. Runs SCIs and deferred work, including what they
 * queue, until no work is queued and no SCI is pending, latched or still in the
 * interrupt controllers, or QUIESCE_NS passes. It then arms the button in the
 * same IF=0 section that found it quiet, so any later SCI or work is a fresh
 * event. Presses delivered meanwhile are dropped. */
static void drain_and_arm(void)
{
  uint64_t deadline = arch_monotonic_ns() + QUIESCE_NS;
  for (;;) {
    service_sci();
    run_work(work_queued);
    uint64_t flags = cpu_save_interrupts();
    bool quiet = !work_head && !sci_pending && !io_apic_sci_requested();
    if (quiet || arch_monotonic_ns() >= deadline) {
      button_pressed = false;
      button_armed = true;
      cpu_restore_interrupts(flags);
      if (!quiet && !quiesce_logged) {
        quiesce_logged = true;
        kwarn("ACPI: events still arriving after 1 s; power button armed anyway\n");
      }
      return;
    }
    cpu_restore_interrupts(flags);
  }
}

/* Top level. Returns only when the operation failed. A press during the
 * operation is serviced here and dropped, so it does not start another. */
static enum call_status run_power(enum acpi_power_action action, bool force)
{
  button_armed = false;
  enum call_status status = acpi_power_run(action, force);
  drain_and_arm();
  return status;
}

/* Top level only, like deferred work. Returns only when the operation failed,
 * after completing the request. */
static void run_power_request(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct acpi_power_request *request = power_started ? NULL : power_request;
  power_started = true;
  cpu_restore_interrupts(flags);
  if (!request) {
    return;
  }

  flags = cpu_save_interrupts();
  bool started = forwarder_operation_start(request->request.operation);
  cpu_restore_interrupts(flags);
  enum call_status status = started ? run_power(request->action, false) : CALL_DENIED;
  flags = cpu_save_interrupts();
  power_request = NULL;
  request->status = status;
  bsp_request_complete(&request->request);
  cpu_restore_interrupts(flags);
}

/* Top level only. Like a press, a local operation needs no capability; it
 * returns only when the operation failed. */
static void run_local_request(void)
{
  uint64_t flags = cpu_save_interrupts();
  bool pending = local_pending;
  enum acpi_power_action action = local_action;
  bool force = local_force;
  if (pending) {
    power_running = true;
  }
  cpu_restore_interrupts(flags);
  if (!pending) {
    return;
  }

  enum call_status status = run_power(action, force);
  flags = cpu_save_interrupts();
  power_running = false;
  local_pending = false;
  local_status = status;
  local_failed = true;
  cpu_restore_interrupts(flags);
}

void acpi_button_press(void)
{
  acpi_require_worker();
  uint64_t flags = cpu_save_interrupts();
  if (button_armed && !power_running && !power_request && !local_pending) {
    button_pressed = true;
  }
  cpu_restore_interrupts(flags);
}

static void run_button_press(void)
{
  if (!button_pressed) {
    return;
  }
  button_pressed = false;
  kinfo("power: power button pressed\n");

  uint64_t flags = cpu_save_interrupts();
  power_running = true;
  cpu_restore_interrupts(flags);
  run_power(ACPI_POWER_OFF, false);
  flags = cpu_save_interrupts();
  power_running = false;
  cpu_restore_interrupts(flags);
}

/* After a failed namespace load or initialization ACPI stays unavailable as
 * before, but the worker keeps answering table requests: a broken ACPI is when
 * the tables are wanted. Whether the interpreter still holds them after its
 * own fatal failure is its decision; a reset answers UNAVAILABLE. */
static void serve_tables_only(void)
{
  for (;;) {
    service_sci();
    acpi_tables_run(WORK_PER_TURN);
    sleep_for_events(UINT64_MAX, true);
  }
}

static void acpi_worker(void *argument)
{
  (void)argument;
  if (!load_namespace()) {
    acpi_wifi_unavailable();
    serve_tables_only();
  }
  uacpi_status status = uacpi_install_notify_handler(uacpi_namespace_root(), trace_notify,
                                                     UACPI_NULL);
  if (status != UACPI_STATUS_OK) {
    kerror("ACPI: error: notify handler not installed: %s\n", uacpi_status_to_string(status));
  }
  acpi_ec_start();
  acpi_pci_resources_start();
  acpi_wifi_start();
  acpi_rtl_wifi_start();
  acpi_battery_start();
  acpi_buttons_start();
  acpi_hid_i2c_start();
  acpi_ucsi_start();
  /* Registration/setup can queue boot-latched Notify work. */
  drain_and_arm();
  uint64_t flags = cpu_save_interrupts();
  ready = true;
  cpu_restore_interrupts(flags);
  for (;;) {
    service_sci();
    run_work(WORK_PER_TURN);
    acpi_tables_run(WORK_PER_TURN);
    run_power_request();
    run_local_request();
    run_button_press();
    uint64_t now = arch_monotonic_ns();
    uint64_t sci_due = rearm_sci(now);
    uint64_t poll_due = acpi_battery_poll(now);
    uint64_t ucsi_due = acpi_ucsi_poll(now);
    if (ucsi_due < poll_due) {
      poll_due = ucsi_due;
    }
    sleep_for_events(sci_due < poll_due ? sci_due : poll_due, true);
  }
}
