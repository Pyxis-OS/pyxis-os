#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/random.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/random.h>
#include <kernel/random/generator.h>
#include <kernel/task.h>
#include <kernel/virtio/rng.h>
#include <stdatomic.h>

#define RNG_CALL_LIMIT 8
#define RNG_SEED_WAIT_MS 5000

enum random_source { RANDOM_NONE, RANDOM_VIRTIO, RANDOM_CPU };
static enum random_source source;

enum rng_call_state { RNG_FREE, RNG_QUEUED, RNG_ACTIVE, RNG_DONE };
struct rng_call {
  enum rng_call_state state;
  size_t length;
  uint64_t deadline;
  uint8_t bytes[RANDOM_MAX_BYTES];
  enum call_status status;
  struct task_wait *wait;
  bool cancelled;
};

static struct rng_call *current; /* BSP worker only; detach before completion. */

/* BSP worker only. No caller owns a seed attempt or its source DMA. */
static struct random_generator generator;
static bool seed_required;
static struct {
  uint8_t bytes[RANDOM_GENERATOR_SEED_BYTES];
  size_t filled;
  uint64_t deadline;
  bool active;
} seed;

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
  if (status != CALL_OK) {
    memzero_explicit(call->bytes, sizeof(call->bytes));
  }
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
  memzero_explicit(call, sizeof(*call));
  unlock_rng();
  return status;
}

static void stop_service(const char *reason)
{
  random_generator_clear(&generator);
  memzero_explicit(&seed, sizeof(seed));
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

static void fail_seed(void)
{
  memzero_explicit(&seed, sizeof(seed));
  /* Keep the required reseed latched even if a later request is smaller. */
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  current = NULL;
  for (size_t i = 0; i < RNG_CALL_LIMIT; ++i) {
    struct rng_call *call = &calls[i];
    if (call->state == RNG_QUEUED || call->state == RNG_ACTIVE) {
      enum call_status status = call->cancelled ? CALL_ENDPOINT_CLOSED :
          task_deadline_expired(call->deadline) ? CALL_TIMED_OUT : CALL_UNAVAILABLE;
      complete_call(call, status);
    }
  }
  unlock_rng();
  cpu_restore_interrupts(flags);
}

static bool expire_seed(void)
{
  if (!seed.active || !task_deadline_expired(seed.deadline)) {
    return false;
  }
  fail_seed();
  return true;
}

static void finish_seed(void)
{
  KASSERT(seed.active && seed.filled == sizeof(seed.bytes));
  if (!expire_seed()) {
    random_generator_seed(&generator, seed.bytes, arch_monotonic_ns());
    memzero_explicit(&seed, sizeof(seed));
    seed_required = false;
  }
}

static void generate_reply(void)
{
  struct rng_call *call = current;
  KASSERT(call && !seed_required);
  random_generator_read(&generator, call->bytes, call->length);
  uint64_t flags = cpu_save_interrupts();
  lock_rng();
  current = NULL;
  enum call_status status = call->cancelled ? CALL_ENDPOINT_CLOSED :
      task_deadline_expired(call->deadline) ? CALL_TIMED_OUT : CALL_OK;
  complete_call(call, status);
  unlock_rng();
  cpu_restore_interrupts(flags);
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
  if (seed.active && seed.deadline < deadline) {
    deadline = seed.deadline;
  }
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

static bool fill_cpu_seed(void)
{
  while (seed.active) {
    expire_calls();
    if (expire_seed()) {
      return true;
    }
    uint64_t word = 0;
    enum arch_random_result result = arch_random_word(&word);
    if (result == ARCH_RANDOM_FAILED) {
      memzero_explicit(&word, sizeof(word));
      memzero_explicit(seed.bytes, sizeof(seed.bytes));
      seed.filled = 0;
      if (!arch_random_enabled(ARCH_RANDOM_RDSEED) && !arch_random_enabled(ARCH_RANDOM_RDRAND)) {
        stop_service("no healthy CPU entropy instruction");
        return false;
      }
      /* A failed instruction cannot contribute any byte to the seed. */
      continue;
    }
    if (result == ARCH_RANDOM_EMPTY) {
      memzero_explicit(&word, sizeof(word));
      fail_seed();
      return true;
    }
    size_t length = sizeof(seed.bytes) - seed.filled;
    if (length > sizeof(word)) {
      length = sizeof(word);
    }
    memcpy(seed.bytes + seed.filled, &word, length);
    memzero_explicit(&word, sizeof(word));
    seed.filled += length;
    if (seed.filled == sizeof(seed.bytes)) {
      finish_seed();
    }
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
    klog("virtio-rng: host seed source ready, demand-driven reseeding\n");
  } else {
    if (!arch_random_init()) {
      stop_service("CPU entropy boot self-test failed or instructions unavailable");
      return;
    }
    klog("random: CPU %s ready; boot self-test passed\n",
        arch_random_enabled(ARCH_RANDOM_RDSEED) ?
            (arch_random_enabled(ARCH_RANDOM_RDRAND) ? "RDSEED (RDRAND fallback)" : "RDSEED") :
            "RDRAND");
  }
  for (;;) {
    bool worked = expire_calls();
    worked = expire_seed() || worked;
    enum virtio_rng_result result = VIRTIO_RNG_IDLE;
    if (source == RANDOM_VIRTIO) {
      size_t length = 0;
      result = virtio_rng_poll(seed.active ? seed.bytes + seed.filled : NULL,
          seed.active ? sizeof(seed.bytes) - seed.filled : 0, &length);
      if (result == VIRTIO_RNG_FAILED) {
        stop_service("device status, completion or watchdog failure");
        return;
      }
      if (result == VIRTIO_RNG_COMPLETE) {
        if (seed.active) {
          KASSERT(length <= sizeof(seed.bytes) - seed.filled);
          seed.filled += length;
          if (seed.filled == sizeof(seed.bytes)) {
            finish_seed();
          }
        }
        worked = true;
      }
    }
    if (!current) {
      select_call();
    }
    if (current) {
      if (seed_required || random_generator_due(&generator, current->length,
          arch_monotonic_ns())) {
        seed_required = true;
        /* An old DMA completion must be discarded before a new attempt starts. */
        if (!seed.active && result != VIRTIO_RNG_PENDING) {
          seed.active = true;
          seed.deadline = task_deadline_after_ms(RNG_SEED_WAIT_MS);
          worked = true;
        }
      } else {
        generate_reply();
        worked = true;
      }
    }
    if (seed.active) {
      if (source == RANDOM_CPU) {
        if (!fill_cpu_seed()) {
          return;
        }
        worked = true;
      } else if (result != VIRTIO_RNG_PENDING) {
        virtio_rng_submit(sizeof(seed.bytes) - seed.filled);
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
