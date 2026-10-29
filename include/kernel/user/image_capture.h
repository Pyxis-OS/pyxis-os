#ifndef KERNEL_USER_IMAGE_CAPTURE_H
#define KERNEL_USER_IMAGE_CAPTURE_H

#include <abi/syscall.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct task;

enum image_capture_phase {
  IMAGE_CAPTURE_PRODUCING,
  IMAGE_CAPTURE_READING,
  IMAGE_CAPTURE_CLOSED,
};

/* One owned immutable selected-image copy after the producer hands it off.
 * The producer's fixed I/O owner alone fills it; one retained preparation actor
 * may then read it. Descriptors move with ownership, never duplicate readers. */
struct image_capture {
  uintptr_t address;
  size_t size;
  size_t backing_bytes;
  enum image_capture_phase phase;
  struct task *reader;
  size_t preparation_cpu;
  bool remote_read;
};

/* IF=0, kernel-root preparation actor (or explicit boot owner). The producer
 * has finished all writes and relinquished refill access before begin. Begin
 * records remote readership before exposing the pointer; end closes admission
 * after the final read. Both run once, on the same retained actor and CPU. */
const void *image_capture_read_begin(struct image_capture *capture);
void image_capture_read_end(struct image_capture *capture);

/* BSP, IF=0. Empty output required. Admits the shared per-image byte ceiling,
 * then allocates eager RW/NX whole pages. Failure leaves output empty and frees
 * any partial data backing/reservation. LIMIT for excess, BAD_REQUEST for zero,
 * NO_MEMORY for backing, metadata or virtual-range exhaustion; BUSY on shared
 * structural-owner admission expiry. Waiting may temporarily enable interrupts;
 * the retained producer must hold no short lock. */
enum call_status image_capture_allocate(uint64_t size, struct image_capture *capture);

/* BSP worker, IF=1. Bounded shared-owner admission before allocation;
 * release ownership before returning to storage I/O. */
enum call_status image_capture_allocate_wait(uint64_t size, struct image_capture *capture);

/* IF=0. Producer failures on CPU0 retain the existing fixed-owner cleanup.
 * A closed read phase releases on its preparation CPU; an unread descriptor
 * may also transfer to a retained kernel-root preparation actor. Transfer the
 * whole range and cleanup attribution to that CPU's RAM reaper. AP readership
 * or an AP cleanup owner requires confirmation of the fixed CPU0 producer's
 * translations before destructive effects, even when the AP never read bytes.
 * Uncertainty retains storage and joined cleanup until reboot. Clear the source
 * only after ownership transfer. */
void image_capture_release(struct image_capture *capture);

#endif
