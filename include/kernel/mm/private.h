#ifndef KERNEL_MM_PRIVATE_H
#define KERNEL_MM_PRIVATE_H

#include <abi/memory.h>
#include <abi/jit.h>
#include <abi/syscall.h>
#include <kernel/mm/types.h>

struct process;
struct private_allocation;

/* Non-reused reservation identity shared with native shared views. */
uint64_t private_memory_identity(void);

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

/* Larger allocations are stepped, yield between steps and run on the VM worker.
 * Smaller ones, including any failure unwind, finish in one step. */
static inline bool private_memory_stepped(size_t size)
{
  return size > PRIVATE_MEMORY_STEP_PAGES * PAGE_SIZE;
}

/* BSP, IF=0, holding the process's drained VM mutation transaction. Sizes are
 * nonzero whole pages. Only allocations made here are releasable here; image,
 * startup and initial stack mappings never enter this process-owned list.
 * Allocation sets address to zero on failure. Release requires an exact
 * current address/size. A stepped allocation runs in a BSP kernel task that
 * enables interrupts and yields between steps; the result stays
 * all-or-nothing, and failure unwinds the partial backing in steps. Other
 * allocations never yield. */
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
/* Same drained BSP mutation token. Only sparse identities can yield on release;
 * eager guarded releases and invalid identities finish inline. */
bool private_memory_release_stepped(struct process *process, uint64_t identity);

/* Same caller-owned registry and drained BSP mutation. Reserve adds only a
 * sparse address range; updates operate only on sparse identities. Updates and
 * sparse release run on the VM worker and yield between bounded steps. */
enum call_status private_memory_reserve(struct process *process, size_t size,
    size_t alignment, struct memory_reservation *reservation);
enum call_status private_memory_update(struct process *process,
    const struct memory_subrange *range, bool commit);

/* Drained BSP mutation only. Exclusively pins the whole usable suffix of a
 * guarded reservation; stale/foreign identities reject and an existing pin is
 * BUSY. Pin storage belongs to the task, must be moved rather than duplicated,
 * and holds no short VM activity lease. */
enum call_status private_context_pin(struct process *process, uint64_t identity,
    struct private_context *context);
/* Owning caller or fixed RAM actor, IF=0, after unpublished unwind or
 * root/stack/continuation retirement,
 * before completion. No VM access or mutation. Zero/unacquired context is safe;
 * clears storage. */
void private_context_unpin(struct private_context *context);

/* Caller CPU, IF=0, retained drained process mutation. Sizes are whole pages;
 * separate kinds prevent MEMORY/context grants from changing JIT allocations. */
enum call_status private_jit_allocate(struct process *process, size_t size,
    struct jit_region *region);
enum call_status private_jit_protect(struct process *process, uint64_t identity,
    size_t offset, size_t size, enum jit_state state);
enum call_status private_jit_release(struct process *process, uint64_t identity, size_t *bytes);

/* Exclusive retired/preparing process owner, IF=0, after all private backing
 * is reclaimed. Frees records in bounded steps, yielding without a VM loan. */
void private_memory_discard_records(struct process *process);

#endif
