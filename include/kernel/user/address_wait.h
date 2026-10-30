#ifndef KERNEL_USER_ADDRESS_WAIT_H
#define KERNEL_USER_ADDRESS_WAIT_H

#include <abi/thread.h>
#include <abi/syscall.h>
#include <stddef.h>
#include <stdint.h>

struct task_wait;
struct process;
struct vm_space;
struct user_buffer_lease;

enum address_wait_result {
  ADDRESS_WAIT_PENDING,
  ADDRESS_WAIT_WOKEN,
  ADDRESS_WAIT_TIMED_OUT,
  ADDRESS_WAIT_WITHDRAWN,
  ADDRESS_WAIT_STOPPED,
};

/* Embedded in cold task storage. Bucket exclusion owns the key/link/result;
 * scheduler queue exclusion owns its association with the task and timer.
 * The task's execution lifetime retains process storage, never a mapping lease.
 * A notifier detaches first and never accesses the record after wake returns. */
struct address_wait_record {
  struct address_wait_record *next;
  struct task_wait *wait;
  struct process *process;
  uintptr_t address;
  uint64_t incarnation;
  size_t word_size;
  size_t bucket;
  enum address_wait_result result;
  bool linked;
};

/* IF=0, kernel-staged request, live packet lease. Takes ownership and releases
 * on every result; an enrolled wait releases after bucket unlock, before sleep. */
enum call_status address_wait_leased(const struct thread_address_wait *request,
    struct user_buffer_lease *lease);

/* IF=0, kernel-staged request. Borrows the live packet lease without releasing it;
 * the caller retains it through any checked reply copy. Never sleeps. */
enum call_status address_wake_leased(const struct thread_address_wake *request, uint64_t *woken,
    const struct user_buffer_lease *lease);

/* IF=0, no scheduler queue guard, outside interrupt/fault entry. Timeout callers
 * run on the waiter's scheduler stack before it can execute/reuse the record;
 * stop callers hold process lifetime exclusion. The saved bucket is immutable
 * for this wait. Another terminal action may already have detached it. */
void address_wait_cancel(struct address_wait_record *record, size_t bucket,
    enum address_wait_result result);

/* Drained private VM owner after validation/preflight, before mapping/frame reuse.
 * Only registrations on affected pages withdraw. No user access or allocation. */
void address_wait_withdraw(struct vm_space *space, uintptr_t base, size_t bytes);

#endif
