#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/random.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/random.h>
#include <kernel/task.h>
#include <kernel/virtio/rng.h>
#include <stdatomic.h>

#define RNG_CALL_LIMIT 8

enum random_source { RANDOM_NONE, RANDOM_VIRTIO, RANDOM_CPU };
static enum random_source source;

enum rng_call_state { RNG_FREE, RNG_QUEUED, RNG_ACTIVE, RNG_DONE };
struct rng_call {
  enum rng_call_state state;
  size_t length, filled;
  uint64_t deadline;
  uint8_t bytes[RANDOM_MAX_BYTES];
  enum call_status status;
  struct task_wait *wait;
  bool cancelled;
};

static struct rng_call *current; /* BSP worker only; detach before completion. */

/* Shared request/notification state. All users hold the lock with IF=0.
 * DONE still owns its slot until the original caller consumes it. */
static struct rng_call calls[RNG_CALL_LIMIT];
static atomic_bool locked;
static bool accepting, notified;
static struct task_wait *worker_wait;

static void lock_rng(void)
{
  while (atomic_exchange_explicit(&locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_rng(void)
{
  atomic_store_explicit(&locked, false, memory_order_release);
}

/* Lock held, IF=0. Remember wakeups arriving before worker wait publication. */
static void notify_worker(void)
{
  notified = true;
  struct task_wait *wait = worker_wait;
  worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

void random_notify(void)
{
  lock_rng();
  notify_worker();
  unlock_rng();
}

/* Lock held. The caller may consume and reuse the slot as soon as we unlock. */
static void complete_call(struct rng_call *call, enum call_status status)
{
  call->status = status;
  call->state = RNG_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
}

enum call_status random_read(void *bytes, size_t length, uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (length > RANDOM_MAX_BYTES) {
    return CALL_LIMIT;
  }
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (deadline - now > RANDOM_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  if (!length) {
    return CALL_OK;
  }

  lock_rng();
  if (!accepting) {
    unlock_rng();
    return CALL_UNAVAILABLE;
  }
  struct rng_call *call = NULL;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_FREE) {
      call = &calls[i];
      break;
    }
  }
  if (!call) {
    unlock_rng();
    return CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct rng_call){
    .state = RNG_QUEUED, .length = length, .deadline = deadline, .wait = wait,
  };
  notify_worker();
  unlock_rng();
  task_wait_sleep_interruptible(wait);

  lock_rng();
  while (call->state != RNG_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    notify_worker();
    unlock_rng();
    task_wait_sleep(wait);
    lock_rng();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK) {
    memcpy(bytes, call->bytes, length);
  }
  *call = (struct rng_call){0};
  unlock_rng();
  return status;
}

static void stop_service(const char *reason)
{
  uint64_t flags = cpu_save_interrupts();
  current = NULL;
  lock_rng();
  accepting = false;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_QUEUED || calls[i].state == RNG_ACTIVE) {
      complete_call(&calls[i], CALL_UNAVAILABLE);
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
  if (source == RANDOM_VIRTIO) {
    virtio_rng_stop(reason);
  } else {
    klog("random: %s; CPU source unavailable until reboot\n", reason);
  }
}

static bool expire_calls(void)
{
  bool worked = false;
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    struct rng_call *call = &calls[i];
    if ((call->state == RNG_QUEUED || call->state == RNG_ACTIVE) &&
        (call->cancelled || task_deadline_expired(call->deadline))) {
      if (current == call) {
        /* The DMA buffer is separate. A late completion will be discarded,
         * never attributed to a caller that reuses this slot. */
        current = NULL;
      }
      complete_call(call, call->cancelled ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT);
      worked = true;
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
  return worked;
}

static void finish_chunk(size_t length)
{
  struct rng_call *call = current;
  if (call) {
    KASSERT(length <= call->length - call->filled);
    call->filled += length;
    if (call->filled == call->length) {
      uint64_t flags = cpu_save_interrupts();
      lock_rng();
      current = NULL;
      complete_call(call, task_deadline_expired(call->deadline) ? CALL_TIMED_OUT : CALL_OK);
      unlock_rng();
      cpu_restore_interrupts(flags);
    }
  }
}

static void select_call(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if (calls[i].state == RNG_QUEUED) {
      calls[i].state = RNG_ACTIVE;
      current = &calls[i];
      break;
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
}

static void wait_for_work(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  uint64_t deadline = source == RANDOM_VIRTIO ? virtio_rng_deadline() : UINT64_MAX;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    if ((calls[i].state == RNG_QUEUED || calls[i].state == RNG_ACTIVE) &&
        calls[i].deadline < deadline) {
      deadline = calls[i].deadline;
    }
  }
  if (notified) {
    notified = false;
    unlock_rng();
  } else {
    struct task_wait *wait = task_wait_prepare();
    KASSERT(!worker_wait);
    worker_wait = wait;
    unlock_rng();
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    lock_rng();
    worker_wait = NULL;
    notified = false;
    unlock_rng();
  }
  cpu_restore_interrupts(flags);
}

static bool fill_cpu(void)
{
  struct rng_call *call = current;
  if (!call) {
    return true;
  }
  while (current) {
    expire_calls();
    if (!current) {
      return true;
    }
    uint64_t word;
    enum arch_random_result result = arch_random_word(&word);
    if (result == ARCH_RANDOM_FAILED) {
      stop_service("CPU entropy health check failed");
      return false;
    }
    if (result == ARCH_RANDOM_EMPTY) {
      uint64_t flags = cpu_save_interrupts();
      lock_rng();
      current = NULL;
      complete_call(call, task_deadline_expired(call->deadline) ? CALL_TIMED_OUT : CALL_UNAVAILABLE);
      unlock_rng();
      cpu_restore_interrupts(flags);
      return true;
    }
    size_t length = call->length - call->filled;
    if (length > sizeof(word)) {
      length = sizeof(word);
    }
    memcpy(call->bytes + call->filled, &word, length);
    finish_chunk(length);
  }
  return true;
}

static void entropy_worker(void *argument)
{
  (void)argument;
  if (source == RANDOM_VIRTIO) {
    if (!virtio_rng_activate()) {
      stop_service("cannot activate transport");
      return;
    }
    klog("virtio-rng: host random source ready, demand-driven reads\n");
  } else {
    if (!arch_random_init()) {
      stop_service("CPU entropy boot self-test failed or instructions unavailable");
      return;
    }
    klog("random: CPU %s ready; boot self-test passed\n",
        arch_random_has_rdseed() ? "RDSEED (RDRAND fallback when supported)" : "RDRAND");
  }
  for (;;) {
    bool worked = expire_calls();
    if (source == RANDOM_VIRTIO) {
      struct rng_call *call = current;
      size_t length = 0;
      enum virtio_rng_result result = virtio_rng_poll(
          call ? call->bytes + call->filled : NULL,
          call ? call->length - call->filled : 0, &length);
      if (result == VIRTIO_RNG_FAILED) {
        stop_service("device status, completion or watchdog failure");
        return;
      }
      if (result == VIRTIO_RNG_COMPLETE) {
        finish_chunk(length);
        worked = true;
      }
      if (result != VIRTIO_RNG_PENDING) {
        if (!current) {
          select_call();
        }
        if (current && !task_deadline_expired(current->deadline)) {
          virtio_rng_submit(current->length - current->filled);
        }
        worked = worked || current;
      }
    } else {
      if (!current) {
        select_call();
      }
      if (current) {
        if (!fill_cpu()) {
          return;
        }
        worked = true;
      }
    }
    if (worked) {
      kernel_task_sleep_until(arch_monotonic_ns());
    } else {
      wait_for_work();
    }
  }
}

void random_prepare(const struct boot_info *boot)
{
  enum virtio_rng_preparation result = virtio_rng_prepare(boot);
  source = result == VIRTIO_RNG_ABSENT ? RANDOM_CPU :
      result == VIRTIO_RNG_PREPARED ? RANDOM_VIRTIO : RANDOM_NONE;
}

void random_start(void)
{
  if (source == RANDOM_NONE) {
    return;
  }
  enum mm_result result = kernel_task_create(entropy_worker, NULL);
  if (result != MM_OK) {
    klog("random: cannot create worker (error %u); reads unavailable\n", (unsigned)result);
    return;
  }
  accepting = true; /* Before scheduling; queued callers await activation/self-test. */
}
