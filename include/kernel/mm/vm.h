#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H
#include <kernel/mm/types.h>

#define VM_MAX_RANGES 256

struct vm_stats {
  size_t total_pages, reserved_pages, backed_pages;
  size_t range_records;
};

void vm_init(void);
/* Sizes are nonzero, rounded up to pages; alignment is a power of two >= a page.
 * Ranges must be released whole with the same rounded size. On failure, output
 * pointers are NULL. Metadata exhaustion returns MM_NO_MEMORY.
 * A reservation owns no frames. Caller mappings must be removed before release. */
enum mm_result vm_reserve(size_t bytes, size_t alignment, void **result);
enum mm_result vm_release(void *base, size_t bytes);
/* Backing is eager, zeroed and owned by VM. Failure leaves a bare reservation.
 * Do not mutate VM-owned mappings using the low-level arch interface. */
enum mm_result vm_back(void *base, size_t bytes, unsigned permissions);
enum mm_result vm_alloc(size_t bytes, size_t alignment, unsigned permissions,
                        void **result);
enum mm_result vm_free(void *base, size_t bytes);
struct vm_stats vm_get_stats(void);

#endif
