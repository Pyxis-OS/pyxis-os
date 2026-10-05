#ifndef KERNEL_MM_PRIVATE_H
#define KERNEL_MM_PRIVATE_H

#include <kernel/mm/types.h>

struct process;

/* The process's own task in a syscall, IF=0, on whichever CPU runs it; that
 * CPU has the private address space active and no other CPU uses it. Sizes are
 * nonzero whole pages. Only allocations made here are releasable here; image,
 * startup and initial stack mappings never enter this process-owned list.
 * Allocation sets address to zero on failure. Release requires an exact
 * current address/size. */
enum mm_result private_memory_allocate(struct process *process, size_t size,
                                        uintptr_t *address);
enum mm_result private_memory_release(struct process *process, uintptr_t address,
                                       size_t size);

/* BSP, IF=0, after vm_space_destroy reclaimed all backing. Frees only records. */
void private_memory_discard_records(struct process *process);

#endif
