#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <kernel/audio.h>
#include <kernel/log.h>
#include <kernel/object/audio.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "internal.h"

#define AUDIO_WATCHDOG_MS 5
#define AUDIO_HEADPHONE_SETTLE_MS 75
#define QEMU_SERVICE_LIMIT_NS UINT64_C(20000000)
#define AUDIO_RECOVERY_STREAK_NS UINT64_C(2000000000)
#define AUDIO_RECOVERY_LOG_NS UINT64_C(10000000000)
/* Initialization attempts, each after a checked link reset. Tuning, not ABI. */
#define AUDIO_SETUP_ATTEMPTS 3
#define HDA_BYTES_PER_SECOND (HDA_RATE * HDA_FRAME_BYTES)
#define QEMU_COMMIT_LIMIT_NS UINT64_C(1000000)
#define NATIVE_COMMIT_MS 20
#define NATIVE_COMMIT_LIMIT_NS (UINT64_C(1000000) * NATIVE_COMMIT_MS)
#define NATIVE_COMMIT_PROGRESS_BYTES (HDA_RATE * HDA_FRAME_BYTES * NATIVE_COMMIT_MS / 1000)
#define HDA_WALLCLOCK_TICKS_PER_MS 24000
#define QEMU_CODEC_BURST_BYTES 8192

static struct hda_controller controller;
static struct hda_route route;
static struct task_wait *worker_wait;
static struct bsp_request *request_head, *request_tail;
static uint64_t notifications;
static bool started, worker_live, available;

enum audio_stop_phase {
  AUDIO_STOP_NONE,
  AUDIO_STOP_MUTED,
  AUDIO_STOP_DISABLED,
};

static enum audio_stop_phase stop_phase;
static uint64_t stop_deadline;

/* Observed DMA progress belongs to one RUN epoch. No play cursor or audible
 * drain is exported. Timing violations invalidate the epoch rather than
 * inventing missing laps from a nominal sample rate. */
static uint64_t observed_bytes, refilled_periods, data_end_bytes;
static uint64_t observed_time, progress_time;
static uint32_t observed_position, observed_wallclock;
static uint64_t starts, stops, interrupts, refills, max_commit_ns;
static const char *refill_fault;
/* Set with refill_fault when the stream is intact but the worker was late:
 * a stop, a fresh primed ring and a new RUN epoch recover from it. */
static bool refill_late;
/* Observation, progress and notification-age bound for the current model. */
static uint64_t service_limit_ns = QEMU_SERVICE_LIMIT_NS;
/* A restarted epoch is unproven until it observes one healthy ring lap.
 * Recoveries that keep failing before that form a streak. */
static uint64_t recoveries, streak_start, recovery_logged;
static bool recovery_unproven;

static void audio_worker(void *argument);
static bool stop_output(void);
static bool run_output(void);
static bool observe_progress(const struct hda_stream_position *position,
    const struct hda_irq_event *event, uint64_t now);

void audio_require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(cpu_current() == cpu_bsp() && (flags & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(kernel_task_is_current(audio_worker, &controller));
  cpu_restore_interrupts(flags);
}

void audio_prepare(const struct boot_info *boot)
{
  hda_prepare(&controller, boot);
}

void audio_worker_notify(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(cpu_current() == cpu_bsp());
  ++notifications;
  struct task_wait *wait = worker_wait;
  worker_wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
  cpu_restore_interrupts(flags);
}

void audio_interrupt(void)
{
  if (hda_interrupt(&controller)) {
    ++interrupts;
    audio_worker_notify();
  }
}

bool audio_engine_available(void)
{
  audio_require_worker();
  return available;
}

void audio_request_forward(struct audio_request *request)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && !request->request.next);
  request->request.state = BSP_REQUEST_FORWARDED;
  if (!worker_live) {
    request->result = CALL_UNAVAILABLE;
    request->process = NULL;
    request->audio = NULL;
    bsp_request_complete(&request->request);
    return;
  }
  if (request_tail) {
    request_tail->next = &request->request;
  } else {
    request_head = &request->request;
  }
  request_tail = &request->request;
  audio_worker_notify();
}

static struct audio_request *take_request(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct bsp_request *request = request_head;
  if (request) {
    request_head = request->next;
    request->next = NULL;
    if (!request_head) {
      request_tail = NULL;
    }
  }
  cpu_restore_interrupts(flags);
  return (struct audio_request *)request;
}

static void fail_engine(const char *reason)
{
  audio_require_worker();
  available = false;
  stop_phase = AUDIO_STOP_NONE;
  audio_volume_available(false);
  audio_sessions_fail();
  hda_fail(&controller, reason);
}

static bool codec_stop(void)
{
  return hda_commands_start(&controller) && hda_codec_disable(&controller, &route) &&
      hda_commands_stop(&controller);
}

static bool start_output(void)
{
  if (!hda_commands_start(&controller) || !hda_codec_enable(&controller, &route) ||
      !hda_commands_stop(&controller) || !hda_stream_prepare(&controller)) {
    return false;
  }
  audio_sessions_cleanup();
  audio_volume_apply();
  if (!audio_sessions_pending()) {
    /* Exit can discard all PCM after activation. Settle that output too. */
    return stop_output();
  }
  recovery_unproven = false;
  return run_output();
}

/* Prepared stream: check the native reserve, prime every period and RUN. */
static bool run_output(void)
{
  if (controller.model == HDA_MODEL_NATIVE) {
    uint32_t reserve = controller.fifo_bytes + HDA_FRAME_BYTES + NATIVE_COMMIT_PROGRESS_BYTES;
    if (!controller.fifo_bytes || reserve + HDA_PERIOD_BYTES >= HDA_BUFFER_BYTES) {
      hda_fail(&controller, "native output commit reserve does not fit PCM ring");
      return false;
    }
    /* After a refill the ring holds at least all but one period ahead of the
     * position. Allow observation gaps only while the next reclaimed period
     * can still keep the commit reserve, so a late worker is detected before
     * DMA reaches PCM it has not replaced. */
    uint32_t horizon_bytes = HDA_BUFFER_BYTES - HDA_PERIOD_BYTES - reserve;
    service_limit_ns = UINT64_C(1000000000) * horizon_bytes / HDA_BYTES_PER_SECOND;
    ktrace("audio: native FIFO-bytes=%u reserve-bytes=%u commit-ns=%lu commit-WALCLK=%u horizon-ns=%lu\n",
        controller.fifo_bytes, reserve, NATIVE_COMMIT_LIMIT_NS,
        HDA_WALLCLOCK_TICKS_PER_MS * NATIVE_COMMIT_MS, service_limit_ns);
  }
  uint64_t flags = cpu_save_interrupts();
  controller.start_failure = NULL;
  observed_bytes = refilled_periods = data_end_bytes = 0;
  for (unsigned period = 0; period < HDA_PERIOD_COUNT; ++period) {
    int16_t *output = (void *)(controller.pcm.address + period * HDA_PERIOD_BYTES);
    size_t frames = audio_sessions_mix(output, HDA_PERIOD_FRAMES);
    if (frames) {
      data_end_bytes = period * HDA_PERIOD_BYTES + frames * HDA_FRAME_BYTES;
    }
  }
  bool running = hda_stream_run_locked(&controller);
  struct hda_stream_position position;
  struct hda_irq_event event = {0};
  observed_position = 0;
  observed_wallclock = controller.run_wallclock;
  observed_time = progress_time = controller.run_time;
  running = running && hda_stream_position_locked(&controller, &position, &event) &&
      observe_progress(&position, &event, arch_monotonic_ns());
  cpu_restore_interrupts(flags);
  if (!running) {
    kwarn("audio: output start refused: %s INTCTL=%x SDnCTL/STS=%x LPIB=%x/%x errors=%x\n",
        controller.start_failure ? controller.start_failure :
        refill_fault ? refill_fault : "unknown", controller.start_intctl,
        controller.start_ctl, controller.start_lpib[0], controller.start_lpib[1],
        (unsigned)event.errors);
    hda_log_start_state(&controller);
  }
  if (running) {
    kdebug("audio: RUN read back after %lu ns\n", controller.run_latch_ns);
    ++starts;
    ktrace("audio: RUN periods=%u period-bytes=%u\n", HDA_PERIOD_COUNT, HDA_PERIOD_BYTES);
  }
  return running;
}

static void park_output(void)
{
  stop_phase = AUDIO_STOP_NONE;
  ++stops;
  ktrace("audio: parked starts=%lu stops=%lu IRQs=%lu refills=%lu recoveries=%lu max-commit-ns=%lu\n",
      starts, stops, interrupts, refills, recoveries, max_commit_ns);
}

static bool stop_output(void)
{
  if (!hda_stream_stop(&controller)) {
    return false;
  }
  if (controller.model == HDA_MODEL_NATIVE) {
    bool headphone_active;
    if (!hda_commands_start(&controller) ||
        !hda_codec_stop_mute(&controller, &route, &headphone_active)) {
      return false;
    }
    if (headphone_active) {
      if (!hda_commands_stop(&controller)) {
        return false;
      }
      stop_phase = AUDIO_STOP_MUTED;
      stop_deadline = task_deadline_after_ms(AUDIO_HEADPHONE_SETTLE_MS);
      return true;
    }
  }
  if (!codec_stop()) {
    return false;
  }
  park_output();
  return true;
}

/* A start refused without a controller fault fails the waiting playback, not
 * the engine (owner policy, 2026-10-15): sessions report FAILED, queued PCM is
 * discarded so the worker does not retry by itself, and the output is stopped
 * and parked as usual. A later acquisition tries again. */
static bool abandon_output(void)
{
  audio_sessions_fail();
  uint8_t errors;
  if (!hda_stream_abort(&controller, &errors)) {
    return false;
  }
  kwarn("audio: playback failed to start; sessions failed, engine kept%s\n",
      errors ? " (FIFO/descriptor error)" : "");
  return stop_output();
}

static bool advance_stop(void)
{
  audio_require_worker();
  if (stop_phase == AUDIO_STOP_NONE || !task_deadline_expired(stop_deadline)) {
    return true;
  }
  if (stop_phase == AUDIO_STOP_MUTED) {
    if (!codec_stop()) {
      return false;
    }
    stop_phase = AUDIO_STOP_DISABLED;
    stop_deadline = task_deadline_after_ms(AUDIO_HEADPHONE_SETTLE_MS);
  } else {
    park_output();
  }
  return true;
}

/* IF=0; caller restores IF before failure shutdown. WALCLK is a wrapping
 * device clock, not an absolute consumption counter. Monotonic time bounds
 * its delta independently of the selected kernel clock source. */
static bool observe_progress(const struct hda_stream_position *position,
    const struct hda_irq_event *event, uint64_t now)
{
  uint32_t wall_ticks = position->wallclock - observed_wallclock;
  if (event->errors) {
    refill_fault = "output FIFO/descriptor error";
    return false;
  }
  if (now - observed_time >= service_limit_ns ||
      wall_ticks >= HDA_WALLCLOCK_TICKS_PER_MS * service_limit_ns / 1000000) {
    refill_fault = "output observation exceeded service horizon";
    refill_late = true;
    return false;
  }
  if (event->first_time && now - event->first_time >= service_limit_ns) {
    refill_fault = "output completion notification expired";
    refill_late = true;
    return false;
  }
  uint32_t step = (position->bytes + HDA_BUFFER_BYTES - observed_position) % HDA_BUFFER_BYTES;
  uint32_t to_boundary = HDA_PERIOD_BYTES - observed_position % HDA_PERIOD_BYTES;
  if (step % HDA_FRAME_BYTES) {
    refill_fault = "output position is not frame aligned";
    return false;
  }
  /* Native BCIS can precede link consumption when the descriptor reaches the
   * FIFO. Only QEMU's qualified model requires this boundary correspondence. */
  if (controller.model == HDA_MODEL_QEMU && event->completed && step < to_boundary) {
    refill_fault = "completion without expected boundary advance";
    return false;
  }
  if (UINT64_MAX - observed_bytes < step + 2 * HDA_BUFFER_BYTES) {
    refill_fault = "output epoch exhausted";
    return false;
  }
  observed_bytes += step;
  observed_position = position->bytes;
  observed_wallclock = position->wallclock;
  observed_time = now;
  if (step) {
    progress_time = now;
  }
  controller.irq.completed = false;
  if (!controller.irq.errors) {
    controller.irq.first_time = 0;
  }
  if (now - progress_time >= service_limit_ns) {
    refill_fault = "output progress stalled";
    refill_late = true;
    return false;
  }
  return true;
}

static bool read_refill_position(struct hda_stream_position *position,
    struct hda_irq_event *event)
{
  if (hda_stream_position_locked(&controller, position, event)) {
    return true;
  }
  refill_fault = controller.start_failure ? controller.start_failure :
      "output position observation refused";
  return false;
}

static bool refill_output(void)
{
  struct hda_stream_position position;
  struct hda_irq_event event;
  refill_fault = "output position observation unstable";
  refill_late = false;
  uint64_t flags = cpu_save_interrupts();
  bool safe = read_refill_position(&position, &event) &&
      observe_progress(&position, &event, arch_monotonic_ns());
  cpu_restore_interrupts(flags);
  if (!safe) {
    return false;
  }
  while (refilled_periods < observed_bytes / HDA_PERIOD_BYTES) {
    flags = cpu_save_interrupts();
    bool native = controller.model == HDA_MODEL_NATIVE;
    uint64_t before = arch_monotonic_ns();
    uint32_t before_wallclock = native ? hda_stream_wallclock_locked(&controller) : 0;
    /* A status scan can timestamp a new completion. Age it after the scan;
     * before still bounds the entire commit, including its position scans. */
    safe = read_refill_position(&position, &event) &&
        observe_progress(&position, &event, arch_monotonic_ns());
    uint64_t target = (refilled_periods + HDA_PERIOD_COUNT) * HDA_PERIOD_BYTES;
    uint32_t margin = native ? controller.fifo_bytes + HDA_FRAME_BYTES +
        NATIVE_COMMIT_PROGRESS_BYTES : QEMU_CODEC_BURST_BYTES + HDA_FRAME_BYTES;
    /* Native headroom includes maximum FIFO advance and playback during the
     * whole bounded commit. QEMU retains its qualified burst/clock profile. */
    if (safe && (target <= observed_bytes ||
        target - observed_bytes <= margin)) {
      refill_fault = "output refill safety margin exhausted";
      refill_late = true;
      safe = false;
    }
    if (!safe) {
      cpu_restore_interrupts(flags);
      return false;
    }
    unsigned period = refilled_periods % HDA_PERIOD_COUNT;
    size_t frames = audio_sessions_mix(
        (void *)(controller.pcm.address + period * HDA_PERIOD_BYTES), HDA_PERIOD_FRAMES);
    dma_write_barrier();
    uint64_t after;
    uint32_t wall_ticks;
    if (native) {
      safe = read_refill_position(&position, &event);
      wall_ticks = hda_stream_wallclock_locked(&controller) - before_wallclock;
    } else {
      before_wallclock = position.wallclock;
      safe = read_refill_position(&position, &event);
      wall_ticks = position.wallclock - before_wallclock;
    }
    /* The scan may have created event.first_time; do not age it with a time
     * from before MMIO. The commit limit includes the complete observation. */
    after = arch_monotonic_ns();
    if (after - before > max_commit_ns) {
      max_commit_ns = after - before;
    }
    uint64_t commit_limit = native ? NATIVE_COMMIT_LIMIT_NS : QEMU_COMMIT_LIMIT_NS;
    uint32_t wall_limit = HDA_WALLCLOCK_TICKS_PER_MS * (native ? NATIVE_COMMIT_MS : 1);
    if (safe && (after - before >= commit_limit || wall_ticks >= wall_limit)) {
      refill_fault = "output DMA commit exceeded clock limit";
      refill_late = true;
      safe = false;
    }
    safe = safe && observe_progress(&position, &event, after);
    uint32_t final_margin = native ? controller.fifo_bytes + HDA_FRAME_BYTES : 0;
    if (safe && (observed_bytes >= target || target - observed_bytes <= final_margin)) {
      refill_fault = "output reached period during DMA commit";
      refill_late = true;
      safe = false;
    }
    if (frames) {
      data_end_bytes = target + frames * HDA_FRAME_BYTES;
    }
    ++refilled_periods;
    ++refills;
    cpu_restore_interrupts(flags);
    if (!safe) {
      return false;
    }
  }
  if (recovery_unproven && observed_bytes >= HDA_BUFFER_BYTES) {
    recovery_unproven = false;
  }
  /* Let a ring of zeros follow the last queued PCM. This bounds the internal
   * tail for the QEMU backend; it is deliberately not an audible drain API. */
  if (!audio_sessions_pending() && observed_bytes >= data_end_bytes &&
      observed_bytes - data_end_bytes >= HDA_BUFFER_BYTES) {
    refill_fault = "output stop failed";
    return stop_output();
  }
  return true;
}

/* A late worker is not a device fault. Stop and reset the stream (a refused
 * reset or a FIFO/descriptor error fails the controller there), then prime the
 * whole ring afresh and start a new RUN epoch: priming overwrites every
 * period, so PCM from before the stop never plays again. Sessions keep their
 * queues and count one discontinuity. Restarts that keep failing before a
 * healthy lap mean the cause is not transient, so they fail the engine. */
static bool recover_output(void)
{
  uint64_t now = arch_monotonic_ns();
  if (!recovery_unproven) {
    streak_start = now;
  } else if (now - streak_start >= AUDIO_RECOVERY_STREAK_NS) {
    refill_fault = "output kept missing service after repeated recovery";
    return false;
  }
  if (!hda_stream_stop(&controller)) {
    return false;
  }
  ++recoveries;
  audio_sessions_discontinuity();
  if (recoveries == 1 || now - recovery_logged >= AUDIO_RECOVERY_LOG_NS) {
    recovery_logged = now;
    kwarn("audio: %s; output restarted (%lu since boot)\n", refill_fault, recoveries);
  }
  audio_sessions_cleanup();
  if (!audio_sessions_pending()) {
    refill_fault = "output stop failed";
    return stop_output();
  }
  refill_fault = "output restart failed";
  recovery_unproven = true;
  return hda_stream_prepare(&controller) && run_output();
}

/* Codec setup can miss a readiness window on a busy BSP. Like Linux, reset
 * the link and try again a bounded number of times; fail only when the reset
 * cannot be verified or every attempt fails. */
static bool initialize_controller(void)
{
  for (unsigned attempt = 1;; ++attempt) {
    controller.setup_attempt = true;
    controller.setup_failure = NULL;
    bool ready = hda_link_start(&controller) && hda_commands_start(&controller) &&
        hda_codec_discover(&controller, &route) && hda_commands_stop(&controller);
    controller.setup_attempt = false;
    if (ready) {
      return true;
    }
    const char *reason = controller.setup_failure ? controller.setup_failure :
        "initialization failed";
    if (attempt == AUDIO_SETUP_ATTEMPTS) {
      hda_fail(&controller, reason);
      return false;
    }
    kwarn("hda: setup attempt %u failed: %s; resetting link\n", attempt, reason);
    if (!hda_link_reset(&controller)) {
      hda_fail(&controller, "link reset after failed setup not verified");
      return false;
    }
  }
}

static void audio_worker(void *argument)
{
  (void)argument;
  audio_require_worker();
  if (controller.prepared) {
    if (initialize_controller()) {
      available = true;
      audio_volume_available(true);
      if (controller.model == HDA_MODEL_NATIVE) {
        kinfo("audio: ready codec=%x cad=%u speaker=%u/DAC=%u headphone=%u/DAC=%u rate=%u format=%x; output idle\n",
            route.vendor, (unsigned)route.codec, (unsigned)route.pin, (unsigned)route.converter,
            (unsigned)route.headphone_pin, (unsigned)route.headphone_converter,
            HDA_RATE, HDA_STREAM_FORMAT);
      } else {
        kinfo("audio: ready codec=%x cad=%u pin=%u DAC=%u rate=%u format=%x; output idle\n",
            route.vendor, (unsigned)route.codec, (unsigned)route.pin,
            (unsigned)route.converter, HDA_RATE, HDA_STREAM_FORMAT);
      }
    }
  }
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    uint64_t observed_notifications = notifications;
    cpu_restore_interrupts(flags);
    audio_sessions_cleanup();
    audio_volume_apply();
    if (available && controller.stream_running && !refill_output() &&
        !(refill_late && recover_output())) {
      fail_engine(refill_fault);
    }
    struct audio_request *request = take_request();
    if (request) {
      flags = cpu_save_interrupts();
      struct execution_group *previous = object_cleanup_enter(request->request.cleanup_group);
      cpu_restore_interrupts(flags);
      struct forwarder_operation *operation_previous = task_forwarder_set(request->request.operation);
      audio_session_request_execute(request);
      task_forwarder_set(operation_previous);
      flags = cpu_save_interrupts();
      object_cleanup_leave(previous);
      bsp_request_complete(&request->request);
      cpu_restore_interrupts(flags);
    }
    if (available && !controller.stream_running) {
      /* Accepted PCM wins over either settle deadline. Activation samples the
       * jack afresh and retains the selected pin if it is still biased. */
      if (audio_sessions_pending()) {
        stop_phase = AUDIO_STOP_NONE;
        if (!start_output() && (controller.failed || !abandon_output())) {
          fail_engine("output activation failed");
        }
      } else if (!advance_stop()) {
        fail_engine("output stop failed");
      }
    }
    flags = cpu_save_interrupts();
    if (request_head || observed_notifications != notifications) {
      cpu_restore_interrupts(flags);
      continue;
    }
    worker_wait = task_wait_prepare();
    struct task_wait *wait = worker_wait;
    if (available && controller.stream_running) {
      task_wait_sleep_until(wait, task_deadline_after_ms(AUDIO_WATCHDOG_MS));
    } else if (available && stop_phase != AUDIO_STOP_NONE) {
      task_wait_sleep_until(wait, stop_deadline);
    } else {
      task_wait_sleep(wait);
    }
    if (worker_wait == wait) {
      worker_wait = NULL;
    }
    cpu_restore_interrupts(flags);
  }
}

void audio_start(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!started);
  started = true;
  worker_live = kernel_task_create(audio_worker, &controller) == MM_OK;
  if (!worker_live) {
    kerror("audio: worker unavailable; link remains reset and DMA disabled\n");
  }
}
