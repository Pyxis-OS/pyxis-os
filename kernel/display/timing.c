#include "timing.h"
#include "internal.h"
#include <arch/amd/renoir_otg.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/display.h>
#include <kernel/string.h>
#include <kernel/task.h>

#define TIMING_INITIAL_NS UINT64_C(250000000)
#define TIMING_SAMPLE_NS UINT64_C(1000000)
#define TIMING_RETRY_NS UINT64_C(1000000000)
#define TIMING_STALL_MIN_NS UINT64_C(50000000)
#define TIMING_MIN_PERIOD_NS UINT64_C(5000000)
#define TIMING_MAX_PERIOD_NS UINT64_C(50000000)
#define TIMING_DENSE_GAP_NS UINT64_C(4000000)
#define TIMING_GUARD_LINES 8
#define TIMING_FRAME_MASK UINT32_C(0xffffff)
#define TIMING_ADVANCES 4
#define TIMING_RECORDS 600
#define TIMING_PROGRESS_INTERVAL 60

enum timing_mode { TIMING_OFF, TIMING_OBSERVE, TIMING_BLANK };
enum timing_result { TIMING_OBSERVED, TIMING_ADMITTED, TIMING_LATE,
                     TIMING_POLL_EXPIRED, TIMING_UNAVAILABLE };

static enum timing_mode mode;
static bool prepared, permanent_loss, qualification_metrics;
static struct renoir_otg_info info;
static struct display_timing_capability capability;
static struct renoir_otg_sample previous, fit_first;
static uint64_t last_advance_ns, attempt_start_ns, retry_after_ns;
static uint64_t travelled_lines, fit_frames, retained_period_ns, max_bracket_ns;
static uint64_t fit_max_gap_ns, period_error_ns, rate_start_ns;
static uint32_t rate_frame;
static unsigned fit_advances;
static bool have_previous, have_fit, dense_fit, saw_blank, saw_active, attempt_active;
static uint64_t edge_lower_ns, edge_upper_ns;

static struct {
  enum timing_result result;
  bool written, sample_valid, progress;
  uint64_t start_ns, start_upper_ns, spin_ns, coarse_late_ns, bracket_ns;
  uint64_t last_progress_ns, max_progress_gap_ns;
  uint64_t copy_edge_ns, copy_line_ns, copy_blank_ns;
  bool progress_late;
} copy;

struct timing_record {
  uint64_t start_ns, copy_ns, spin_ns, bracket_ns, wake_ns, progress_ns;
  bool start_valid, progress_sample;
};
static struct timing_record records[TIMING_RECORDS];
static uint64_t sorted[TIMING_RECORDS];
static size_t record_count;
static uint64_t admitted_count, unsynchronized_count, invalid_count, progress_late_count;
static uint64_t late_count, expired_count, unavailable_count;
static uint64_t total_written;
static uint64_t rejected_bounds, widest_rejected_ns;

static bool text_is(const char *text, const char *expected)
{
  size_t size = strlen(expected);
  return text && strnlen(text, size + 1) == size && !memcmp(text, expected, size);
}

static uint64_t midpoint(const struct renoir_otg_sample *sample)
{
  return sample->before_ns + (sample->after_ns - sample->before_ns) / 2;
}

static void reset_fit(uint64_t now)
{
  have_fit = false;
  travelled_lines = fit_frames = 0;
  fit_advances = 0;
  fit_max_gap_ns = 0;
  dense_fit = true;
  saw_blank = saw_active = false;
  attempt_start_ns = now;
  attempt_active = true;
}

static void lose_timing(const char *reason, uint64_t now, bool permanent)
{
  if (capability.hardware) {
    ktrace("display-timing: hardware observation lost: %s\n", reason);
  }
  capability = (struct display_timing_capability){0};
  have_previous = false;
  reset_fit(now);
  attempt_active = false;
  retry_after_ns = now + TIMING_RETRY_NS;
  permanent_loss |= permanent;
}

static bool set_edges(const struct renoir_otg_sample *sample)
{
  uint64_t line_ns = capability.period_ns / sample->mode.v_total;
  uint64_t lines = (sample->v_position + sample->mode.v_total -
      sample->mode.blank_start) % sample->mode.v_total;
  uint64_t elapsed = lines * line_ns;
  /* Vertical position identifies a line, not the pixel within that line.
   * Include read bracketing and the fitted period's one-line endpoint error. */
  uint64_t uncertainty = line_ns + max_bracket_ns +
      (lines + 1) * period_error_ns / sample->mode.v_total;
  edge_lower_ns = sample->before_ns > elapsed + uncertainty ?
      sample->before_ns - elapsed - uncertainty : 0;
  edge_upper_ns = sample->after_ns > elapsed ? sample->after_ns - elapsed + uncertainty : 0;
  capability.uncertainty_ns = edge_upper_ns >= edge_lower_ns ?
      edge_upper_ns - edge_lower_ns : 0;
  uint64_t blank_lines = (sample->mode.blank_end + sample->mode.v_total -
      sample->mode.blank_start) % sample->mode.v_total;
  bool usable = edge_lower_ns && edge_upper_ns >= edge_lower_ns &&
      blank_lines > TIMING_GUARD_LINES && capability.uncertainty_ns <
      (blank_lines - TIMING_GUARD_LINES) * line_ns;
  if (!usable) {
    ++rejected_bounds;
    widest_rejected_ns = MAX(widest_rejected_ns, capability.uncertainty_ns);
  }
  return usable;
}

static bool observe(struct renoir_otg_sample *sample)
{
  enum renoir_otg_result result = renoir_otg_read(sample);
  uint64_t now = arch_monotonic_ns();
  if (result == RENOIR_OTG_UNAVAILABLE) {
    lose_timing(renoir_otg_reason(), now, true);
    return false;
  }
  if (result == RENOIR_OTG_RETRY) {
    if (capability.hardware && now - last_advance_ns >
        MAX(TIMING_STALL_MIN_NS, capability.period_ns * 3)) {
      lose_timing("no coherent advancing sample", now, false);
    }
    return false;
  }
  uint64_t stamp = midpoint(sample);
  max_bracket_ns = MAX(max_bracket_ns, sample->after_ns - sample->before_ns);
  uint32_t delta = have_previous ?
      (sample->frame_count - previous.frame_count) & TIMING_FRAME_MASK : 0;
  if (have_previous && delta > TIMING_FRAME_MASK / 2) {
    lose_timing("frame counter moved backwards", now, false);
    return false;
  }
  if (!have_previous || delta) {
    last_advance_ns = stamp;
  }
  if (capability.hardware) {
    uint64_t elapsed = stamp - midpoint(&previous);
    uint64_t expected_frames = elapsed / capability.period_ns;
    if (delta > expected_frames + 2 || (expected_frames > 2 && delta + 2 < expected_frames)) {
      lose_timing("implausible counter advancement", now, false);
      return false;
    }
    uint64_t rate_elapsed = stamp - rate_start_ns;
    if (rate_elapsed >= capability.period_ns * TIMING_ADVANCES) {
      uint64_t frames = (sample->frame_count - rate_frame) & TIMING_FRAME_MASK;
      uint64_t expected = frames * capability.period_ns;
      uint64_t error = rate_elapsed > expected ? rate_elapsed - expected : expected - rate_elapsed;
      if (error > capability.period_ns * 2) {
        lose_timing("counter rate changed", now, false);
        return false;
      }
      if (rate_elapsed >= TIMING_RETRY_NS) {
        rate_start_ns = stamp;
        rate_frame = sample->frame_count;
      }
    }
    if (stamp - last_advance_ns > MAX(TIMING_STALL_MIN_NS, capability.period_ns * 3)) {
      lose_timing("frame counter stalled", now, false);
      return false;
    }
    capability.frame_sequence += delta;
    capability.timestamp_ns = stamp;
    if (!set_edges(sample)) {
      lose_timing("uncertainty exceeds guarded blank budget", now, false);
      return false;
    }
  } else if (stamp >= retry_after_ns) {
    if (!attempt_active) {
      reset_fit(stamp);
    }
    if (stamp - attempt_start_ns > TIMING_INITIAL_NS) {
      attempt_active = false;
      have_fit = false;
      retry_after_ns = stamp + TIMING_RETRY_NS;
    } else {
      saw_blank |= sample->in_blank;
      saw_active |= !sample->in_blank;
      if (!have_fit) {
        fit_first = *sample;
        have_fit = true;
      } else if (have_previous) {
        uint64_t gap = stamp - midpoint(&previous);
        fit_max_gap_ns = MAX(fit_max_gap_ns, gap);
        if (gap < TIMING_DENSE_GAP_NS && delta <= 1) {
          /* Dense startup sampling counts vertical wraps independently of
           * the frame counter's unspecified phase. */
          travelled_lines += (sample->v_position + sample->mode.v_total -
              previous.v_position) % sample->mode.v_total;
        } else {
          dense_fit = false;
        }
        fit_frames += delta;
        fit_advances += delta != 0;
      }
      if (fit_advances >= TIMING_ADVANCES && saw_blank && saw_active) {
        uint64_t elapsed = stamp - midpoint(&fit_first);
        uint64_t period = dense_fit && travelled_lines ?
            elapsed * sample->mode.v_total / travelled_lines : 0;
        /* Sparse runtime requalification may reuse an unchanged mode's
         * dense calibration only when counter rate remains consistent. */
        if (!period && fit_frames) {
          /* Sparse samples fit counter rate; their phase uncertainty is
           * retained explicitly. It can report hardware timing while being
           * too imprecise to admit a guarded copy. */
          period = elapsed / fit_frames;
          if (retained_period_ns) {
            uint64_t error = period > retained_period_ns ? period - retained_period_ns :
                retained_period_ns - period;
            if (error < retained_period_ns / 20) {
              period = retained_period_ns;
            }
          }
        }
        if (period >= TIMING_MIN_PERIOD_NS && period <= TIMING_MAX_PERIOD_NS) {
          period_error_ns = dense_fit ?
              (period * 2 + max_bracket_ns * 2 * sample->mode.v_total) /
              MAX(travelled_lines, UINT64_C(1)) :
              (fit_max_gap_ns * 2 + TIMING_MAX_PERIOD_NS) / fit_frames;
          capability = (struct display_timing_capability){
            .frame_sequence = sample->frame_count,
            .timestamp_ns = stamp, .period_ns = period,
          };
          if (!set_edges(sample)) {
            capability = (struct display_timing_capability){0};
            previous = *sample;
            have_previous = true;
            return true;
          }
          capability.hardware = true;
          retained_period_ns = period;
          last_advance_ns = stamp;
          rate_start_ns = stamp;
          rate_frame = sample->frame_count;
          if (qualification_metrics) {
            klog("display-timing: hardware otg=%u period=%lu line=%lu blank=%lu uncertainty=%lu ns\n",
                info.mode.otg, period, period / info.mode.v_total,
                ((info.mode.blank_end + info.mode.v_total - info.mode.blank_start) %
                 info.mode.v_total) * (period / info.mode.v_total), capability.uncertainty_ns);
          }
        }
      }
    }
  }
  previous = *sample;
  have_previous = true;
  return true;
}

void display_timing_prepare(const struct boot_info *boot, bool firmware_backend,
    const char *option, bool metrics)
{
  qualification_metrics = metrics;
  mode = TIMING_OBSERVE;
  if (text_is(option, "off")) {
    mode = TIMING_OFF;
  } else if (text_is(option, "blank")) {
    mode = TIMING_BLANK;
  } else if (option && !text_is(option, "observe")) {
    klog("display-timing: invalid option; retaining observation only\n");
  }
  if (mode == TIMING_OFF) {
    klog("display-timing: off; unsynchronized copies\n");
    return;
  }
  if (!firmware_backend || !renoir_otg_prepare(boot, &info)) {
    klog("display-timing: unavailable: %s; existing backend timing retained\n",
        firmware_backend ? renoir_otg_reason() : "not a firmware framebuffer");
    return;
  }
  prepared = true;
  klog("display-timing: %s; read-only Renoir %x:%x.%u BAR5=0x%lx OTG%u\n",
      mode == TIMING_BLANK ? "experimental blank-start copies" : "observation; unsynchronized copies",
      info.address.bus, info.address.device, info.address.function, info.bar5, info.mode.otg);
  if (qualification_metrics) {
    klog("renoir-otg: active=%ux%u total=%ux%u blank=%u..%u control=0x%x\n",
        info.mode.h_active, info.mode.v_active, info.mode.h_total, info.mode.v_total,
        info.mode.blank_start, info.mode.blank_end, info.mode.raw_control);
    klog("renoir-otg: H_TOTAL=%x H_BLANK=%x H_TIMING=%x V_TOTAL=%x V_BLANK=%x\n",
        info.mode.raw_h_total, info.mode.raw_h_blank, info.mode.raw_h_timing,
        info.mode.raw_v_total, info.mode.raw_v_blank);
    klog("renoir-otg: V_MIN=%x V_MAX=%x V_CONTROL=%x INTERLACE=%x\n",
        info.mode.raw_v_total_min, info.mode.raw_v_total_max,
        info.mode.raw_v_total_control, info.mode.raw_interlace);
  }
}

void display_timing_start(void)
{
  if (!prepared) {
    return;
  }
  uint64_t deadline = arch_monotonic_ns() + TIMING_INITIAL_NS;
  reset_fit(arch_monotonic_ns());
  while (!capability.hardware && !permanent_loss && !display_is_panicking()) {
    struct renoir_otg_sample sample;
    observe(&sample);
    uint64_t now = arch_monotonic_ns();
    if (now >= deadline) {
      break;
    }
    kernel_task_sleep_until(MIN(now + TIMING_SAMPLE_NS, deadline));
  }
  if (qualification_metrics) {
    if (!capability.hardware) {
      klog("display-timing: counters unqualified; unsynchronized copies\n");
    }
    klog("renoir-otg: frame=%u v=%u position=%x status=%x advances=%u bracket=%lu ns\n",
        previous.frame_count, previous.v_position, previous.raw_position,
        previous.raw_status, fit_advances, max_bracket_ns);
  }
}

static bool start_fits(const struct renoir_otg_sample *sample, uint64_t now)
{
  if (!capability.hardware || !sample->in_blank || !edge_lower_ns || now < edge_lower_ns) {
    return false;
  }
  uint64_t line = capability.period_ns / sample->mode.v_total;
  uint64_t blank_lines = (sample->mode.blank_end + sample->mode.v_total -
      sample->mode.blank_start) % sample->mode.v_total;
  if (blank_lines <= TIMING_GUARD_LINES) {
    return false;
  }
  uint64_t budget = (blank_lines - TIMING_GUARD_LINES) * line;
  return now - edge_lower_ns < budget;
}

void display_timing_begin_copy(void)
{
  copy = (typeof(copy)){.result = TIMING_OBSERVED};
}

bool display_timing_front_write(void)
{
  if (!prepared || permanent_loss) {
    return false;
  }
  struct renoir_otg_sample sample;
  bool valid = observe(&sample) && capability.hardware;
  uint64_t now = arch_monotonic_ns();
  copy.progress = (++total_written % TIMING_PROGRESS_INTERVAL) == 0;
  copy.sample_valid = valid && edge_lower_ns && now >= edge_lower_ns;
  copy.bracket_ns = sample.after_ns - sample.before_ns;
  if (copy.sample_valid) {
    copy.start_upper_ns = now - edge_lower_ns;
    copy.copy_edge_ns = edge_lower_ns;
    copy.copy_line_ns = capability.period_ns / info.mode.v_total;
    copy.copy_blank_ns = ((info.mode.blank_end + info.mode.v_total - info.mode.blank_start) %
        info.mode.v_total) * copy.copy_line_ns;
  }
  if (mode == TIMING_BLANK) {
    copy.result = !valid ? TIMING_UNAVAILABLE :
        start_fits(&sample, now) ? TIMING_ADMITTED : TIMING_LATE;
  }
  return true;
}

void display_timing_write_stamp(uint64_t stamp)
{
  copy.written = true;
  copy.start_ns = stamp;
  copy.last_progress_ns = stamp;
  if (copy.sample_valid) {
    copy.start_upper_ns = stamp - copy.copy_edge_ns;
    if (copy.result == TIMING_ADMITTED && copy.start_upper_ns +
        TIMING_GUARD_LINES * copy.copy_line_ns >= copy.copy_blank_ns) {
      copy.result = TIMING_LATE;
    }
  }
}

void display_timing_progress(size_t rows)
{
  if (!copy.written || !copy.progress) {
    return;
  }
  uint64_t now = arch_monotonic_ns();
  copy.max_progress_gap_ns = MAX(copy.max_progress_gap_ns, now - copy.last_progress_ns);
  copy.last_progress_ns = now;
  if (copy.sample_valid && rows) {
    uint64_t row_fetch = copy.copy_edge_ns + copy.copy_blank_ns +
        (rows - 1) * copy.copy_line_ns;
    uint64_t guard = TIMING_GUARD_LINES * copy.copy_line_ns;
    copy.progress_late |= now + guard >= row_fetch;
  }
}

enum timing_metric { METRIC_START, METRIC_COPY, METRIC_SPIN,
                     METRIC_BRACKET, METRIC_WAKE, METRIC_PROGRESS };

static uint64_t metric_value(const struct timing_record *record, enum timing_metric metric)
{
  switch (metric) {
    case METRIC_START: return record->start_ns;
    case METRIC_COPY: return record->copy_ns;
    case METRIC_SPIN: return record->spin_ns;
    case METRIC_BRACKET: return record->bracket_ns;
    case METRIC_WAKE: return record->wake_ns;
    case METRIC_PROGRESS: return record->progress_ns;
  }
  return 0;
}

static void print_metric(const char *name, enum timing_metric metric)
{
  size_t count = 0;
  for (size_t i = 0; i < record_count; ++i) {
    if ((metric == METRIC_START && !records[i].start_valid) ||
        (metric == METRIC_PROGRESS && !records[i].progress_sample)) {
      continue;
    }
    sorted[count++] = metric_value(&records[i], metric);
  }
  if (!count) {
    klog("display-timing: %s unavailable\n", name);
    return;
  }
  for (size_t i = 1; i < count; ++i) {
    uint64_t value = sorted[i];
    size_t j = i;
    while (j && sorted[j - 1] > value) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = value;
  }
  klog("display-timing: %s n=%zu p50=%lu p95=%lu p99=%lu max=%lu ns\n", name, count,
      sorted[count / 2], sorted[count * 95 / 100],
      sorted[count * 99 / 100], sorted[count - 1]);
}

void display_timing_finish(void)
{
  if (!copy.written) {
    return;
  }
  copy.written = false;
  if (display_is_panicking()) {
    return;
  }
  /* Match the reference's completion measurement, rather than time a load
   * which could pass outstanding WC stores. The ordinary fence remains too. */
  __asm__ volatile("mfence; lfence" : : : "memory");
  uint64_t done = arch_monotonic_ns();
  if (copy.result == TIMING_ADMITTED && mode == TIMING_BLANK) {
    ++admitted_count;
  } else {
    ++unsynchronized_count;
  }
  late_count += copy.result == TIMING_LATE;
  expired_count += copy.result == TIMING_POLL_EXPIRED;
  unavailable_count += copy.result == TIMING_UNAVAILABLE;
  progress_late_count += copy.progress_late;
  if (!copy.sample_valid) {
    ++invalid_count;
  }
  records[record_count++] = (struct timing_record){
    .start_ns = copy.start_upper_ns, .copy_ns = done - copy.start_ns,
    .spin_ns = copy.spin_ns, .bracket_ns = copy.bracket_ns,
    .wake_ns = copy.coarse_late_ns, .progress_ns = copy.max_progress_gap_ns,
    .start_valid = copy.sample_valid, .progress_sample = copy.progress,
  };
  if (record_count != TIMING_RECORDS) {
    return;
  }
  if (qualification_metrics) {
    klog("display-timing: source=%s mode=%s samples=%zu admitted=%lu unsync=%lu invalid=%lu progress-late=%lu frame=%u v=%u position=%x status=%x uncertainty=%lu rejected-bounds=%lu reject-max=%lu ns\n",
        capability.hardware ? "hardware" : "unavailable", mode == TIMING_BLANK ? "blank" : "observe",
        record_count, admitted_count, unsynchronized_count, invalid_count, progress_late_count,
        previous.frame_count, previous.v_position, previous.raw_position, previous.raw_status,
        capability.uncertainty_ns, rejected_bounds, widest_rejected_ns);
    klog("display-timing: missed-starts late=%lu poll-expired=%lu unqualified=%lu\n",
        late_count, expired_count, unavailable_count);
    print_metric("start-upper", METRIC_START);
    print_metric("copy-fence", METRIC_COPY);
    print_metric("spin", METRIC_SPIN);
    print_metric("read-bracket", METRIC_BRACKET);
    print_metric("wake-late", METRIC_WAKE);
    print_metric("issued-prefix-gap", METRIC_PROGRESS);
  }
  record_count = 0;
  admitted_count = unsynchronized_count = invalid_count = progress_late_count = 0;
  late_count = expired_count = unavailable_count = 0;
  rejected_bounds = widest_rejected_ns = 0;
}

const struct display_timing_capability *display_timing_capability(void)
{
  return &capability;
}
