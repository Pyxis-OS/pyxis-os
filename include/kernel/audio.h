#ifndef KERNEL_AUDIO_H
#define KERNEL_AUDIO_H

struct boot_info;

/* Optional HDA engine; QEMU controller/codec bring-up only. BSP/IF=0 preparation
 * precedes AP startup; start creates the sole DMA owner after task_init(). */
void audio_prepare(const struct boot_info *boot);
void audio_start(void);

#endif
