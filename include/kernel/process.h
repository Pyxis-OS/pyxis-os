#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/mm/types.h>

struct space;
struct vm_space;

/* One user task per process. The process owns address_space; space is borrowed
 * from the initialized space set and must outlive the process. */
struct process {
  struct space *space;
  struct vm_space *address_space;
};

/* BSP, IF=0, after heap and space initialization. The caller exclusively owns
 * an inactive private address space. Success transfers it to the new process;
 * failure leaves it with the caller and clears *result when non-NULL. */
enum mm_result process_create(struct space *space, struct vm_space *address_space,
                              struct process **result);

/* BSP, IF=0, with exclusive ownership of an unsubmitted or retired process.
 * A submitted process belongs to its task until the BSP reaper receives it
 * after leaving the task stack and private root. Success frees the address
 * space and process; failure leaves both intact. The owning space survives. */
enum mm_result process_destroy(struct process *process);

#endif
