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
/* BSP kernel task, IF=1, after SMP startup, no output/queue/allocator locks.
 * Caller must first stop every remote access to the retiring shared ranges,
 * including cached pointers, and prevent refills until unmap. Ranges remain
 * mapped and their frames owned throughout the wait. Success permits vm_free
 * with IF=0; timeout requires retaining the mappings and backing until reboot.
 * This flush changes neither page tables nor VM ownership. */
bool vm_kernel_flush_remote(void);
/* Requires an initialized heap. New spaces share kernel mappings and start
 * with an empty user area. Failure sets *result to NULL. Allocation, queries
 * and mutation are BSP-only, IF=0, and asserted for the kernel space, whose
 * reused ranges have no remote invalidation; no VM operations from fault
 * handlers. Published process mapping changes require its BSP mutation token
 * after acknowledged root departure and borrower drainage. Unpublished loading
 * and drained retirement remain exclusively owned; resumption reloads CR3. */
enum mm_result vm_space_create(struct vm_space **result);
/* Private activation requires a process root reservation. Always flushes that
 * CPU's TLB, including shared kernel translations; acknowledge departure only
 * after activating the kernel root on the permanent scheduler stack. */
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
 * TLB flushes for task stacks. Other retirement requires explicit quiescence
 * and vm_kernel_flush_remote(); concurrent remote mapping changes remain
 * unsupported. */
enum mm_result vm_reserve(struct vm_space *space, size_t bytes, size_t alignment,
                          uintptr_t *result);
enum mm_result vm_reserve_at(struct vm_space *space, uintptr_t base, size_t bytes);
enum mm_result vm_release(struct vm_space *space, uintptr_t base, size_t bytes);
/* Backs a whole bare reservation or a contained suffix, leaving its lower
 * prefix unmapped. One reservation owns that suffix; release/free remain whole.
 * Backing is eager, zeroed and owned by VM. Failure leaves a bare reservation.
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
/* Boot-only driver-owned scanout page, RW/NX/WC; caller excludes aliases. */
enum mm_result vm_map_scanout(uintptr_t base, phys_addr_t physical);
/* BSP IF=0, before publication: scan all kernel VM ranges for physical aliases. */
bool vm_kernel_physical_overlap(phys_addr_t physical, size_t bytes);
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

/* IF=0 on the executing CPU, under a caller-owned short mapping lease. Returns
 * a scalar incarnation for an aligned 4/8-byte word in VM-owned user RW RAM.
 * Caller-owned/display mappings and unbacked guards are ineligible. Failure
 * clears *token. Retain only the scalar token while sleeping, never a range
 * pointer or mapping lease.
 * Owned backing and eligibility upgrades assign non-reused tokens; withdrawal
 * detaches affected registrations before unmapping or removing write access. */
bool vm_address_wait_token(struct vm_space *space, uintptr_t address,
                           size_t word_size, uint64_t *token);

/* Parking bucket exclusion, IF=0, with process/space storage retained. Account
 * once per linked registration, detach before notification. This derived count
 * lets drained VM mutation skip withdrawal scans when no registrations exist;
 * it is neither a mapping lease nor incarnation authority. */
void vm_wait_enroll(struct vm_space *space);
void vm_wait_detach(struct vm_space *space);

struct vm_stats vm_get_stats(const struct vm_space *space);

#endif
