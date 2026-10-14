#ifndef KERNEL_MM_PRIVATE_H
#define KERNEL_MM_PRIVATE_H

#include <abi/memory.h>
#include <abi/syscall.h>
#include <kernel/mm/types.h>

struct process;
struct private_allocation;

struct private_context {
  struct private_allocation *allocation;
  uint64_t identity;
  uintptr_t address;
  size_t size;
  uintptr_t usable_address;
  size_t usable_size;
};

/* Pages zeroed and mapped per interrupts-off allocation step. Tuning, not ABI. */
#define PRIVATE_MEMORY_STEP_PAGES 256

/* BSP, IF=0, holding the process's drained VM mutation transaction. Sizes are
 * nonzero whole pages. Only allocations made here are releasable here; image,
 * startup and initial stack mappings never enter this process-owned list.
 * Allocation sets address to zero on failure. Release requires an exact
 * current address/size. An allocation larger than one step runs in a BSP
 * kernel task that enables interrupts and yields between steps; the result
 * stays all-or-nothing, and failure unwinds the partial backing in steps. */
enum mm_result private_memory_allocate(struct process *process, size_t size,
                                        uintptr_t *address);
enum mm_result private_memory_release(struct process *process, uintptr_t address,
                                       size_t size);

/* Same drained BSP mutation token. Size is nonzero whole-page usable backing;
 * the full reservation adds one lower guard. Output is zero on failure. */
enum call_status private_memory_allocate_guarded(struct process *process, size_t size,
    struct memory_reservation *reservation);
enum call_status private_memory_release_reservation(struct process *process,
    uint64_t identity);

/* Drained BSP mutation only. Exclusively pins the whole usable suffix of a
 * guarded reservation; stale/foreign identities reject and an existing pin is
 * BUSY. Pin storage belongs to the task, must be moved rather than duplicated,
 * and holds no short VM activity lease. */
enum call_status private_context_pin(struct process *process, uint64_t identity,
    struct private_context *context);
/* BSP, IF=0, for unpublished unwind or after root/stack/continuation retirement,
 * before completion. No VM access or mutation. Zero/unacquired context is safe;
 * clears storage. */
void private_context_unpin(struct private_context *context);

/* BSP, IF=0, after vm_space_destroy reclaimed all backing. Frees only records. */
void private_memory_discard_records(struct process *process);

#endif
