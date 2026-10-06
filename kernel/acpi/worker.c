#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/io_apic.h>
#include <arch/smp.h>
#include <kernel/acpi.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <uacpi/kernel_api.h>
#include <uacpi/uacpi.h>
#include "host.h"

#define NANOSECONDS_PER_MICROSECOND 1000

struct acpi_work {
  struct acpi_work *next;
  uacpi_work_handler handler;
  uacpi_handle context;
};

/* Shared with SCI interrupt entry. The SCI is routed to the BSP and the worker
 * is pinned there, so IF=0 on the BSP serializes both without a lock. */
static struct task_wait *worker_wait;
static bool sci_pending;

/* Worker only. The registration's address is uACPI's opaque SCI handle. */
static struct sci_registration {
  uacpi_interrupt_handler handler;
  uacpi_handle context;
} sci;
static struct acpi_work *work_head, *work_tail;
static bool running_work;
static char worker_identity;

/* Bootstrap only, then read by the worker. */
static uint64_t rsdp_physical;

static void acpi_worker(void *argument);

void acpi_prepare(const struct boot_info *boot)
{
  if (!boot->acpi_rsdp) {
    klog("ACPI: no RSDP; ACPI unavailable\n");
    return;
  }
  if (acpi_map_prepare(boot)) {
    rsdp_physical = boot->acpi_rsdp;
  }
}

void acpi_start(void)
{
  if (!rsdp_physical) {
    return;
  }
  enum mm_result result = kernel_task_create(acpi_worker, NULL);
  if (result != MM_OK) {
    klog("ACPI: cannot create worker (error %u); ACPI unavailable\n", (unsigned)result);
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

void acpi_interrupt(void)
{
  KASSERT(arch_cpu_index() == 0);
  io_apic_sci_mask();
  sci_pending = true;
  wake_worker();
}

/* Runs uACPI's handler with IF=0, as interrupt entry would. A level-triggered
 * SCI that nothing claims would fire again at once, so it stays masked. */
static void service_sci(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (sci_pending) {
    sci_pending = false;
    if (sci.handler) {
      if (sci.handler(sci.context) == UACPI_INTERRUPT_HANDLED) {
        io_apic_sci_unmask();
      } else {
        klog("ACPI: error: unclaimed SCI; ACPI events stopped\n");
      }
    }
  }
  cpu_restore_interrupts(flags);
}

/* IF=1. Sleep until an SCI arrives, deferred work is queued when WORK_WAKES,
 * or DEADLINE passes. */
static void sleep_for_events(uint64_t deadline, bool work_wakes)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  if (!sci_pending && !(work_wakes && work_head)) {
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
    klog("ACPI: interrupt %u is not the routed SCI; handler not installed\n", irq);
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
  io_apic_sci_mask();
  sci = (struct sci_registration){0};
  sci_pending = false;
  cpu_restore_interrupts(flags);
  return UACPI_STATUS_OK;
}

/* Both kinds of work run in order on the BSP worker, which also meets GPE
 * work's requirement to run on CPU 0. */
uacpi_status uacpi_kernel_schedule_work(uacpi_work_type type, uacpi_work_handler handler,
                                        uacpi_handle context)
{
  (void)type;
  acpi_require_worker();
  struct acpi_work *work = uacpi_kernel_alloc(sizeof(*work));
  if (!work) {
    return UACPI_STATUS_OUT_OF_MEMORY;
  }
  *work = (struct acpi_work){.handler = handler, .context = context};
  if (work_tail) {
    work_tail->next = work;
  } else {
    work_head = work;
  }
  work_tail = work;
  return UACPI_STATUS_OK;
}

static void run_work(void)
{
  while (work_head) {
    service_sci();
    struct acpi_work *work = work_head;
    work_head = work->next;
    if (!work_head) {
      work_tail = NULL;
    }
    running_work = true;
    work->handler(work->context);
    running_work = false;
    uacpi_kernel_free(work, sizeof(*work));
  }
}

/* Nothing runs concurrently with the worker, so in-flight interrupts and work
 * are the pending SCI and the queue, completed here in that order. */
uacpi_status uacpi_kernel_wait_for_work_completion(void)
{
  acpi_require_worker();
  if (running_work) {
    klog("ACPI: error: deferred work cannot wait for deferred work\n");
    return UACPI_STATUS_INTERNAL_ERROR;
  }
  service_sci();
  run_work();
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
 * handler) and runs _STA/_INI. GPEs and fixed events stay disabled. */
static bool load_namespace(void)
{
  uint64_t start = arch_monotonic_ns();
  const char *step = "initialization";
  uacpi_status status = uacpi_initialize(0);
  if (status == UACPI_STATUS_OK) {
    step = "namespace load";
    status = uacpi_namespace_load();
  }
  if (status == UACPI_STATUS_OK) {
    step = "namespace initialization";
    status = uacpi_namespace_initialize();
  }
  if (status != UACPI_STATUS_OK) {
    klog("ACPI: error: %s failed: %s; ACPI unavailable\n", step,
         uacpi_status_to_string(status));
    return false;
  }

  uint64_t elapsed = arch_monotonic_ns() - start;
  struct acpi_heap_use heap = acpi_heap_use();
  klog("ACPI: uACPI %u.%u.%u namespace ready in %llu us; heap %zu bytes in %zu blocks, "
       "firmware window %zu pages\n", UACPI_MAJOR, UACPI_MINOR, UACPI_PATCH,
       (unsigned long long)(elapsed / NANOSECONDS_PER_MICROSECOND), heap.bytes,
       heap.blocks, acpi_map_pages());
  return true;
}

static void acpi_worker(void *argument)
{
  (void)argument;
  if (!load_namespace()) {
    return;
  }
  for (;;) {
    service_sci();
    run_work();
    sleep_for_events(UINT64_MAX, true);
  }
}
