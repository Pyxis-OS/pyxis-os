#ifndef KERNEL_USER_IMAGE_CAPTURE_H
#define KERNEL_USER_IMAGE_CAPTURE_H

#include <abi/syscall.h>
#include <stddef.h>
#include <stdint.h>

/* One owned selected-image copy, separate from eager child image backing.
 * Only BSP code may access address: APs transfer this descriptor through shared
 * request metadata without reading its buffer. Reused kernel mappings therefore
 * have no remote translations to retire. All fields are zero when empty. */
struct image_capture {
  uintptr_t address;
  size_t size;
  size_t backing_bytes;
};

/* BSP, IF=0. Empty output required. Admits the shared per-image byte ceiling,
 * then allocates eager RW/NX whole pages. Failure leaves output empty and frees
 * any partial data backing/reservation. LIMIT for excess, BAD_REQUEST for zero,
 * NO_MEMORY for backing, metadata or virtual-range exhaustion. */
enum call_status image_capture_allocate(uint64_t size, struct image_capture *capture);

/* BSP, IF=0, after every BSP reader has finished. Releases the whole mapping and
 * owned data pages, then clears the descriptor. Empty is harmless. Shared kernel
 * page-table ancestors retain their ordinary VM lifetime. */
void image_capture_release(struct image_capture *capture);

#endif
