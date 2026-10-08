#ifndef KERNEL_VIRTIO_RNG_H
#define KERNEL_VIRTIO_RNG_H

#include <abi/random.h>
#include <kernel/boot.h>

enum virtio_rng_preparation {
  VIRTIO_RNG_ABSENT, VIRTIO_RNG_UNAVAILABLE, VIRTIO_RNG_PREPARED,
};
enum virtio_rng_result {
  VIRTIO_RNG_IDLE, VIRTIO_RNG_PENDING, VIRTIO_RNG_COMPLETE, VIRTIO_RNG_FAILED,
};

/* BSP/IF=0, before AP startup. Presence and preparation failure are distinct. */
enum virtio_rng_preparation virtio_rng_prepare(const struct boot_info *boot);
/* BSP worker only. Poll copies/discards returned DMA data and clears the buffer;
 * NULL bytes discards an abandoned source request. Stop retains resources. */
bool virtio_rng_activate(void);
enum virtio_rng_result virtio_rng_poll(void *bytes, size_t capacity, size_t *length);
void virtio_rng_submit(size_t length);
uint64_t virtio_rng_deadline(void);
void virtio_rng_stop(const char *reason);
/* BSP interrupt entry, IF=0; wakes the worker, never accesses queue buffers. */
void virtio_rng_interrupt(void);

#endif
