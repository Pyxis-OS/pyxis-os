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
#define AUDIO_SERVICE_LIMIT_NS UINT64_C(20000000)
#define AUDIO_COMMIT_LIMIT_NS UINT64_C(1000000)
#define HDA_WALLCLOCK_TICKS_PER_MS 24000
#define QEMU_CODEC_BURST_BYTES 8192

static struct hda_controller controller;
static struct hda_route route;
static struct task_wait *worker_wait;
static struct bsp_request *request_head, *request_tail;
static uint64_t notifications;
static bool started, worker_live, available;

/* Observed DMA progress belongs to one RUN epoch. No play cursor or audible
 * drain is exported. Timing violations invalidate the epoch rather than
 * inventing missing laps from a nominal sample rate. */
static uint64_t observed_bytes, refilled_periods, data_end_bytes;
static uint64_t observed_time, progress_time;
static uint32_t observed_position, observed_wallclock;
static uint64_t starts, stops, interrupts, refills, max_commit_ns;
static const char *refill_fault;

static void audio_worker(void *argument);

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
  if (!audio_sessions_pending()) {
    return codec_stop();
  }
  uint64_t flags = cpu_save_interrupts();
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
  struct hda_irq_event event;
  running = running && hda_stream_position_locked(&controller, &position, &event) &&
      !event.errors && !position.bytes;
  observed_position = 0;
  observed_wallclock = running ? position.wallclock : 0;
  observed_time = progress_time = arch_monotonic_ns();
  cpu_restore_interrupts(flags);
  if (running) {
    ++starts;
    ktrace("audio: RUN periods=%u period-bytes=%u\n", HDA_PERIOD_COUNT, HDA_PERIOD_BYTES);
  }
  return running;
}

static bool stop_output(void)
{
  if (!hda_stream_stop(&controller) || !codec_stop()) {
    return false;
  }
  ++stops;
  ktrace("audio: parked starts=%lu stops=%lu IRQs=%lu refills=%lu max-commit-ns=%lu\n",
      starts, stops, interrupts, refills, max_commit_ns);
  return true;
}

/* IF=0; caller restores IF before failure shutdown. WALCLK is a wrapping
 * device clock, not an absolute consumption counter. HPET bounds its delta. */
static bool observe_progress(const struct hda_stream_position *position,
    const struct hda_irq_event *event, uint64_t now)
{
  uint32_t wall_ticks = position->wallclock - observed_wallclock;
  if (event->errors) {
    refill_fault = "output FIFO/descriptor error";
    return false;
  }
  if (now - observed_time >= AUDIO_SERVICE_LIMIT_NS ||
      wall_ticks >= HDA_WALLCLOCK_TICKS_PER_MS * (AUDIO_SERVICE_LIMIT_NS / 1000000)) {
    refill_fault = "output observation exceeded service horizon";
    return false;
  }
  if (event->first_time && now - event->first_time >= AUDIO_SERVICE_LIMIT_NS) {
    refill_fault = "output completion notification expired";
    return false;
  }
  uint32_t step = (position->bytes + HDA_BUFFER_BYTES - observed_position) % HDA_BUFFER_BYTES;
  uint32_t to_boundary = HDA_PERIOD_BYTES - observed_position % HDA_PERIOD_BYTES;
  if (step % HDA_FRAME_BYTES) {
    refill_fault = "output position is not frame aligned";
    return false;
  }
  if (event->completed && step < to_boundary) {
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
  if (now - progress_time >= AUDIO_SERVICE_LIMIT_NS) {
    refill_fault = "output progress stalled";
    return false;
  }
  return true;
}

static bool refill_output(void)
{
  struct hda_stream_position position;
  struct hda_irq_event event;
  refill_fault = "output position observation unstable";
  uint64_t flags = cpu_save_interrupts();
  bool safe = hda_stream_position_locked(&controller, &position, &event) &&
      observe_progress(&position, &event, arch_monotonic_ns());
  cpu_restore_interrupts(flags);
  if (!safe) {
    return false;
  }
  while (refilled_periods < observed_bytes / HDA_PERIOD_BYTES) {
    flags = cpu_save_interrupts();
    uint64_t before = arch_monotonic_ns();
    safe = hda_stream_position_locked(&controller, &position, &event) &&
        observe_progress(&position, &event, before);
    uint64_t target = (refilled_periods + HDA_PERIOD_COUNT) * HDA_PERIOD_BYTES;
    /* Keep a whole codec burst ahead of the reclaimed period. The postcheck
     * rejects a commit window spanning more than one codec timer interval. */
    if (safe && (target <= observed_bytes ||
        target - observed_bytes <= QEMU_CODEC_BURST_BYTES + HDA_FRAME_BYTES)) {
      refill_fault = "output refill safety margin exhausted";
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
    uint64_t after = arch_monotonic_ns();
    if (after - before > max_commit_ns) {
      max_commit_ns = after - before;
    }
    uint32_t before_wallclock = position.wallclock;
    safe = hda_stream_position_locked(&controller, &position, &event);
    if (safe && (after - before >= AUDIO_COMMIT_LIMIT_NS ||
        (uint32_t)(position.wallclock - before_wallclock) >= HDA_WALLCLOCK_TICKS_PER_MS)) {
      refill_fault = "output DMA commit exceeded clock limit";
      safe = false;
    }
    safe = safe && observe_progress(&position, &event, after);
    if (safe && observed_bytes >= target) {
      refill_fault = "output reached period during DMA commit";
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
  /* Let a ring of zeros follow the last queued PCM. This bounds the internal
   * tail for the QEMU backend; it is deliberately not an audible drain API. */
  if (!audio_sessions_pending() && observed_bytes >= data_end_bytes &&
      observed_bytes - data_end_bytes >= HDA_BUFFER_BYTES) {
    refill_fault = "output stop failed";
    return stop_output();
  }
  return true;
}

static void audio_worker(void *argument)
{
  (void)argument;
  audio_require_worker();
  if (controller.prepared) {
    if (!hda_link_start(&controller) || !hda_commands_start(&controller) ||
        !hda_codec_discover(&controller, &route) || !hda_commands_stop(&controller)) {
      hda_fail(&controller, "initialization failed");
    } else {
      available = true;
      klog("audio: ready codec=%x cad=%u pin=%u DAC=%u rate=%u format=%x; output idle\n",
          route.vendor, (unsigned)route.codec, (unsigned)route.pin,
          (unsigned)route.converter, HDA_RATE, HDA_STREAM_FORMAT);
    }
  }
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    uint64_t observed_notifications = notifications;
    cpu_restore_interrupts(flags);
    audio_sessions_cleanup();
    if (available && controller.stream_running && !refill_output()) {
      fail_engine(refill_fault);
    }
    struct audio_request *request = take_request();
    if (request) {
      flags = cpu_save_interrupts();
      struct execution_group *previous = object_cleanup_enter(request->request.cleanup_group);
      cpu_restore_interrupts(flags);
      audio_session_request_execute(request);
      flags = cpu_save_interrupts();
      object_cleanup_leave(previous);
      bsp_request_complete(&request->request);
      cpu_restore_interrupts(flags);
    }
    if (available && !controller.stream_running && audio_sessions_pending() && !start_output()) {
      fail_engine("output activation failed");
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
    klog("audio: worker unavailable; link remains reset and DMA disabled\n");
  }
}
