#ifndef KERNEL_AUDIO_H
#define KERNEL_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_PERIOD_FRAMES 480

struct boot_info;
struct audio_request;

/* Optional HDA engine; QEMU controller/codec bring-up only. BSP/IF=0 preparation
 * precedes AP startup; start creates the sole DMA owner after task_init(). */
void audio_prepare(const struct boot_info *boot);
void audio_start(void);

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
