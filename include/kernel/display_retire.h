#ifndef KERNEL_DISPLAY_RETIRE_H
#define KERNEL_DISPLAY_RETIRE_H

#include <kernel/spinlock.h>
#include <stdbool.h>
#include <stddef.h>

struct display_frame;
struct process;
struct task_wait;

/* Retained by the caller through actual backing release. Issue all joined
 * transfers before waiting; presenter-leased frames keep their separate lifetime.
 * Unconfirmed retirement leaves this join pending until reboot. */
struct display_retirement {
  struct spinlock guard;
  size_t pending;
  struct task_wait *wait;
  struct process *process;
};

/* IF=0, before first use. No previous transfer or waiter may still own it. */
void display_retirement_init(struct display_retirement *join);
/* IF=0, retained kernel-root task, no shared structural owner held. Returns only
 * after all joined backing is reclaimed. Keep its request/process live while parked. */
void display_retirement_wait(struct display_retirement *join);
/* IF=0, process RAM actor owns PROCESS. True keeps that ownership with caller;
 * false transfers the whole process record to the join until real completion.
 * No access to PROCESS/join may follow a false result. Uncertainty retains both. */
bool display_retirement_join_process(struct display_retirement *join, struct process *process);

/* Boot BSP, IF=0, before user admission. Mandatory queue allocation; existing
 * fixed RAM actors are created separately by process_reaper_init. */
void display_ram_init(void);
/* BSP, IF=0, final reference already released and all accesses/refills closed.
 * Transfers the entire frame and existing group attribution to preparation_cpu.
 * The optional join covers this final backing release, never a future lease. */
void display_frame_retire(struct display_frame *frame, struct display_retirement *join);
/* Current preparation CPU's fixed RAM actor. Pending is IF=0; reap is IF=1.
 * Reap confirms a detached batch before any free, then releases in bounded steps.
 * An unconfirmed batch and all group/join ownership remain quarantined until reboot. */
bool display_ram_pending(void);
void display_ram_reap(void);

#endif
