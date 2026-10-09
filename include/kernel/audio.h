#ifndef KERNEL_AUDIO_H
#define KERNEL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_PERIOD_FRAMES 480

struct boot_info;
struct audio_request;
struct space;

enum audio_volume_action {
  AUDIO_VOLUME_SET_PERCENT,
  AUDIO_VOLUME_ADJUST_PERCENT,
  AUDIO_VOLUME_TOGGLE_MUTE,
};

struct audio_volume_snapshot {
  uint64_t volume_generation;
  unsigned master_percent, space_percent;
  bool master_muted, space_muted, available;
};

/* Trusted bar input/presentation only, BSP with either interrupt state.
 * NULL selects master. Control composes ordered inputs into a pending target;
 * snapshot reports only worker-confirmed state. Playback grants cannot set it. */
bool audio_volume_control(struct space *space, enum audio_volume_action action, int value);
bool audio_volume_snapshot(struct space *space, struct audio_volume_snapshot *snapshot);
/* Sole BSP worker, IF=1: publish readiness and consume coalesced controls. */
void audio_volume_available(bool available);
void audio_volume_apply(void);

/* Optional HDA engine; QEMU controller/codec bring-up only. BSP/IF=0 preparation
 * precedes AP startup; start creates the sole DMA owner after task_init(). */
void audio_prepare(const struct boot_info *boot);
void audio_start(void);
void audio_interrupt(void);
void audio_require_worker(void);

/* BSP executor, IF=0: transfer to the sole audio worker. Notification may be
 * called on the BSP with either interrupt state, after releasing object locks. */
void audio_request_forward(struct audio_request *request);
void audio_worker_notify(void);
/* Sole BSP audio worker, IF=1. Cleanup notices precede requests and mixing. */
bool audio_engine_available(void);
void audio_sessions_cleanup(void);
void audio_session_request_execute(struct audio_request *request);
bool audio_sessions_pending(void);
/* Sole BSP audio worker, IF=0, after validating a safe owned DMA period.
 * Exit invalidation cannot interleave with generation checks, queue consumption
 * and direct output publication. Consumes at most AUDIO_PERIOD_FRAMES per source. */
size_t audio_sessions_mix(int16_t *output, size_t frames);
void audio_sessions_fail(void);

#endif
