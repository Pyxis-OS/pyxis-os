#ifndef KERNEL_VIRTIO_RNG_H
#define KERNEL_VIRTIO_RNG_H

#include <abi/random.h>
#include <abi/syscall.h>
#include <kernel/boot.h>

/* BSP/IF=0: prepare before AP startup, start after task_init(). Missing or
 * failed hardware leaves reads unavailable; boot never needs entropy. */
void virtio_rng_prepare(const struct boot_info *boot);
void virtio_rng_start(void);
/* BSP interrupt entry, IF=0; wakes the worker, never accesses queue buffers. */
void virtio_rng_interrupt(void);
/* Current task, IF=0. Copies out only on complete success. Private caller
 * storage stays local: the worker uses bounded shared slots, not this pointer. */
enum call_status virtio_rng_read(void *bytes, size_t length, uint64_t deadline);

#endif
