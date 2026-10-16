/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel-config.h>
#include <kernel/input/hid_i2c_metrics.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/task.h>

#ifdef CONFIG_HID_I2C_MEASURE
#define METRICS_SAMPLES 16384
#define METRICS_PHASE_MS 10000
#define METRICS_ARM_MS 120000

static const uint32_t intervals[] = {0, 8, 8, 4, 2, 1, 8};
static const char *const phases[] = {
  "baseline-lift", "idle-lift", "move-8", "move-4", "move-2", "move-1", "idle-lift-final",
};

struct hid_i2c_metrics {
  uint64_t deadline, start, last_report, last_poll, service_sum, wakes, polls, frames, backlog;
  uint64_t assertion_start, assertion_span_max;
  uint64_t service_min, service_max, malformed_base, last_malformed;
  uint64_t samples[METRICS_SAMPLES], spacing[METRICS_SAMPLES], poll_spacing[METRICS_SAMPLES];
  size_t sample_count, spacing_count, poll_spacing_count;
  unsigned phase;
  bool started, complete;
};

/* In-place heapsort, only after a phase and outside service timing. */
static void sift(uint64_t *values, size_t root, size_t count)
{
  for (;;) {
    size_t child = root * 2 + 1;
    if (child >= count) {
      return;
    }
    if (child + 1 < count && values[child] < values[child + 1]) {
      ++child;
    }
    if (values[root] >= values[child]) {
      return;
    }
    uint64_t value = values[root];
    values[root] = values[child];
    values[child] = value;
    root = child;
  }
}

static uint64_t median(uint64_t *values, size_t count)
{
  if (!count) {
    return 0;
  }
  for (size_t i = count / 2; i > 0; --i) {
    sift(values, i - 1, count);
  }
  for (size_t i = count; i > 1; --i) {
    uint64_t value = values[0];
    values[0] = values[i - 1];
    values[i - 1] = value;
    sift(values, 0, i - 1);
  }
  return count & 1 ? values[count / 2] :
      values[count / 2 - 1] + (values[count / 2] - values[count / 2 - 1]) / 2;
}

static void finish_phase(struct hid_i2c_metrics *metrics, uint64_t now)
{
  uint64_t service_median = median(metrics->samples, metrics->sample_count);
  uint64_t spacing_median = median(metrics->spacing, metrics->spacing_count);
  uint64_t poll_median = median(metrics->poll_spacing, metrics->poll_spacing_count);
  uint64_t elapsed = now - metrics->start;
  if (metrics->assertion_start && now - metrics->assertion_start > metrics->assertion_span_max) {
    metrics->assertion_span_max = now - metrics->assertion_start;
  }
  /* Ten-second phases permit division by elapsed microseconds without a
   * 128-bit division runtime; denominator rounding is under one microsecond. */
  uint64_t elapsed_us = elapsed / 1000;
  bool cost_valid = elapsed_us && metrics->service_sum <= UINT64_MAX / 1000;
  uint64_t fraction_ppm = cost_valid ? metrics->service_sum * 1000 / elapsed_us : 0;
  kinfo("HID-I2C rate: phase=%s elapsed-ns=%llu wakes=%llu polls=%llu frames=%llu "
      "asserted-after-drain=%llu malformed=%llu\n", phases[metrics->phase],
      (unsigned long long)elapsed, (unsigned long long)metrics->wakes,
      (unsigned long long)metrics->polls, (unsigned long long)metrics->frames,
      (unsigned long long)metrics->backlog,
      (unsigned long long)(metrics->last_malformed - metrics->malformed_base));
  kinfo("HID-I2C cost: phase=%s samples=%zu service-ns=%llu/%llu/%llu mean-ns=%llu "
      "estimated-fraction-ppm=%llu cost-valid=%u clock=%s\n", phases[metrics->phase],
      metrics->sample_count, (unsigned long long)(metrics->polls ? metrics->service_min : 0),
      (unsigned long long)service_median, (unsigned long long)metrics->service_max,
      (unsigned long long)(metrics->polls ? metrics->service_sum / metrics->polls : 0),
      (unsigned long long)fraction_ppm, cost_valid,
      arch_clock_source());
  kinfo("HID-I2C polls: phase=%s interval-ms=%u samples=%zu actual-spacing-ns=%llu/%llu/%llu "
      "sampled-assertion-max-ns=%llu\n", phases[metrics->phase], intervals[metrics->phase],
      metrics->poll_spacing_count,
      (unsigned long long)(metrics->poll_spacing_count ? metrics->poll_spacing[0] : 0),
      (unsigned long long)poll_median,
      (unsigned long long)(metrics->poll_spacing_count ? metrics->poll_spacing[metrics->poll_spacing_count - 1] : 0),
      (unsigned long long)metrics->assertion_span_max);
  kinfo("HID-I2C spacing: phase=%s samples=%zu read-spacing-ns=%llu/%llu/%llu\n",
      phases[metrics->phase], metrics->spacing_count,
      (unsigned long long)(metrics->spacing_count ? metrics->spacing[0] : 0),
      (unsigned long long)spacing_median,
      (unsigned long long)(metrics->spacing_count ? metrics->spacing[metrics->spacing_count - 1] : 0));
}

struct hid_i2c_metrics *hid_i2c_metrics_create(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct hid_i2c_metrics *metrics = kmalloc(sizeof(*metrics));
  if (metrics) {
    *metrics = (struct hid_i2c_metrics){.deadline = task_deadline_after_ms(METRICS_ARM_MS)};
  }
  cpu_restore_interrupts(flags);
  if (metrics) {
    kinfo("HID-I2C rate: armed; sweep starts in %u ms; lift contacts until move-8\n", METRICS_ARM_MS);
  } else {
    kinfo("HID-I2C rate: measurement allocation refused; ordinary polling only\n");
  }
  return metrics;
}

uint32_t hid_i2c_metrics_interval(struct hid_i2c_metrics *metrics)
{
  if (metrics && !metrics->complete) {
    uint64_t now = arch_monotonic_ns();
    if (now >= metrics->deadline) {
      if (metrics->started) {
        finish_phase(metrics, now);
        ++metrics->phase;
      }
      if (metrics->phase == sizeof(intervals) / sizeof(intervals[0])) {
        metrics->complete = true;
        kinfo("HID-I2C rate: complete; estimated fraction excludes scheduler/cache effects\n");
      } else {
        metrics->started = true;
        metrics->start = now;
        metrics->deadline = task_deadline_after_ms(METRICS_PHASE_MS);
        metrics->service_sum = metrics->wakes = metrics->polls = metrics->frames = 0;
        metrics->backlog = metrics->last_report = metrics->last_poll = metrics->service_max = 0;
        metrics->assertion_start = metrics->assertion_span_max = 0;
        metrics->service_min = UINT64_MAX;
        metrics->sample_count = metrics->spacing_count = metrics->poll_spacing_count = 0;
        metrics->malformed_base = metrics->last_malformed;
        kinfo("HID-I2C rate: begin=%s interval-ms=%u duration-ms=%u\n",
            phases[metrics->phase], intervals[metrics->phase], METRICS_PHASE_MS);
      }
    }
    if (metrics->started && !metrics->complete) {
      return intervals[metrics->phase];
    }
  }
  return CONFIG_HID_I2C_POLL_MS;
}

void hid_i2c_metrics_report(struct hid_i2c_metrics *metrics)
{
  if (metrics && metrics->started && !metrics->complete) {
    uint64_t now = arch_monotonic_ns();
    if (metrics->last_report && metrics->spacing_count < METRICS_SAMPLES) {
      metrics->spacing[metrics->spacing_count++] = now - metrics->last_report;
    }
    metrics->last_report = now;
  }
}

void hid_i2c_metrics_poll(struct hid_i2c_metrics *metrics, uint64_t start,
    uint64_t end, bool polled, unsigned frames, bool asserted, uint64_t malformed)
{
  if (metrics) {
    metrics->last_malformed = malformed;
  }
  if (metrics && metrics->started && !metrics->complete) {
    ++metrics->wakes;
    metrics->frames += frames;
    metrics->backlog += asserted;
    if (polled) {
      ++metrics->polls;
      if (metrics->last_poll && metrics->poll_spacing_count < METRICS_SAMPLES) {
        metrics->poll_spacing[metrics->poll_spacing_count++] = start - metrics->last_poll;
      }
      metrics->last_poll = start;
      if (asserted && !metrics->assertion_start) {
        metrics->assertion_start = end;
      } else if (!asserted && metrics->assertion_start) {
        uint64_t span = end - metrics->assertion_start;
        if (span > metrics->assertion_span_max) {
          metrics->assertion_span_max = span;
        }
        metrics->assertion_start = 0;
      }
      uint64_t service = end - start;
      metrics->service_sum += service;
      if (service < metrics->service_min) {
        metrics->service_min = service;
      }
      if (service > metrics->service_max) {
        metrics->service_max = service;
      }
      if (metrics->sample_count < METRICS_SAMPLES) {
        metrics->samples[metrics->sample_count++] = service;
      }
    }
  }
}

#endif
