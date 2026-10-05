#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H
#include <kernel/mm/types.h>

/* Only the kernel uses bounded metadata, to break the VM/heap bootstrap cycle. */
#define VM_MAX_RANGES 256

struct vm_space;

struct vm_stats {
  size_t total_pages, reserved_pages, backed_pages;
  size_t range_records;
};

void vm_init(void);
struct vm_space *vm_kernel_space(void);
/* Requires an initialized heap. New spaces share kernel mappings and start
 * with an empty user area. Failure sets *result to NULL. Allocation, queries
 * and mutation are BSP-only, IF=0, and asserted for the kernel space, whose
 * reused ranges have no remote invalidation; no VM operations from fault
 * handlers. A process's only task may also change its own private space in a
 * syscall, IF=0, on the CPU where that space is active. Otherwise a running
 * task must leave its private root and lend ownership through the scheduler
 * before BSP mutation; resumption reloads CR3. */
enum mm_result vm_space_create(struct vm_space **result);
/* Each CPU may activate the kernel space or a private space it exclusively
 * owns. Always flushes that CPU's TLB, including shared kernel translations. */
enum mm_result vm_space_activate(struct vm_space *space);
/* Rejects kernel/active spaces and reservations with caller-owned mappings.
 * Frees VM-owned backing, private page tables and metadata on success.
 * The active check is local: a remote task must return ownership first. */
enum mm_result vm_space_destroy(struct vm_space *space);

/* Addresses belong to the target space, not necessarily the active one.
 * Sizes are nonzero, rounded up to pages; alignment is a power of two >= a page.
 * Ranges must be released whole with the same rounded size. Address outputs
 * are zero on failure. Metadata exhaustion returns MM_NO_MEMORY.
 * A reservation owns no frames. Caller mappings must be removed before release.
 * Shared kernel mappings must remain stable while another CPU uses them.
 * Publishing a new task and retiring an old one provide the required local
 * TLB flushes for task stacks; arbitrary remote mapping changes are unsupported. */
enum mm_result vm_reserve(struct vm_space *space, size_t bytes, size_t alignment,
                          uintptr_t *result);
enum mm_result vm_reserve_at(struct vm_space *space, uintptr_t base, size_t bytes);
enum mm_result vm_release(struct vm_space *space, uintptr_t base, size_t bytes);
/* Backing is eager, zeroed and owned by VM. Failure leaves a bare reservation.
 * PAGE_USER is required for user backing and forbidden for kernel backing. */
enum mm_result vm_back(struct vm_space *space, uintptr_t base, size_t bytes,
                       unsigned permissions);
enum mm_result vm_alloc(struct vm_space *space, size_t bytes, size_t alignment,
                        unsigned permissions, uintptr_t *result);
enum mm_result vm_alloc_at(struct vm_space *space, uintptr_t base, size_t bytes,
                           unsigned permissions);
enum mm_result vm_free(struct vm_space *space, uintptr_t base, size_t bytes);

/* Map/unmap one page in a bare reservation. Supplied frames remain caller-owned;
 * unmap returns the frame without freeing it. Do not bypass VM's ownership
 * bookkeeping by mutating its spaces through the arch interface. */
enum mm_result vm_map(struct vm_space *space, uintptr_t base, phys_addr_t physical,
                      unsigned permissions);
/* Map a device-owned frame into a bare kernel reservation as RW/NX/uncached.
 * Caller must exclude RAM/cache aliases. Never free MMIO frames through PMM;
 * undo with vm_unmap and release the virtual reservation. Before AP startup. */
enum mm_result vm_map_mmio(uintptr_t base, phys_addr_t physical);
enum mm_result vm_unmap(struct vm_space *space, uintptr_t base,
                        phys_addr_t *physical);
/* Protect one mapped page in a reservation or VM-owned allocation. */
enum mm_result vm_protect(struct vm_space *space, uintptr_t base,
                          unsigned permissions);
enum mm_result vm_query(struct vm_space *space, uintptr_t address,
                        struct page_translation *result);

/* Narrow exception to BSP-only queries: IF=0 on the CPU exclusively executing
 * this private space, whose mappings must stay stable through access. Checks
 * every page for user read/write access without touching VM metadata or
 * scratch mappings. Zero bytes ignores address; the space must still be active. */
bool vm_user_buffer_accessible(struct vm_space *space, uintptr_t address,
                               size_t bytes, bool write);

struct vm_stats vm_get_stats(const struct vm_space *space);

#endif
