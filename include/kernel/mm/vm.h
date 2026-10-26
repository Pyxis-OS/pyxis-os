#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H
#include <kernel/mm/types.h>

/* Only the kernel uses bounded metadata, to break the VM/heap bootstrap cycle. */
#define VM_MAX_RANGES 4096

struct vm_space;

/* Boot-created permanent firmware/native mapping domain. The fixed BSP owner
 * consumes fresh slots monotonically; successful mappings live until reboot,
 * failed fills consume their slots. Generic unmap/protect/release is forbidden.
 * Prepare ancestors before AP startup, so runtime fills never allocate or wait. */
struct vm_kernel_window {
  uintptr_t base;
  size_t bytes, used;
};
enum mm_result vm_kernel_window_prepare(size_t bytes, size_t alignment,
    struct vm_kernel_window *window);
enum mm_result vm_kernel_window_map(struct vm_kernel_window *window,
    phys_addr_t physical, size_t bytes, bool mmio, uintptr_t *address);

/* IF=0. Transfer a whole VM-owned RAM allocation to retained cleanup after all
 * accesses/refills stop (unpublished/local or already confirmed shared proof).
 * Keeps group cleanup attribution; may return before physical reclamation.
 * No task/scheduler/executing stack, MMIO, borrowed frames or uncertain range.
 * Existing reaper performs bounded steps; no new worker. Source descriptor may
 * be cleared only after this transfer. Never reports fabricated MM_OK freeing. */
void vm_kernel_retire(uintptr_t base, size_t bytes);
bool vm_kernel_retire_pending(void);
void vm_kernel_reap(void); /* Existing reaper, IF=1; retains owner across steps. */

/* IF=0 and structural owner held. Permanently retain a failed shared-retirement
 * reservation/mapping; generic destructive operations refuse afterward. The
 * caller also fences its concrete consumer and retains any non-VM backing. */
void vm_kernel_quarantine(uintptr_t base, size_t bytes);

struct vm_stats {
  size_t total_pages, reserved_pages, backed_pages;
  size_t range_records;
  /* Retained VM records/tokens, excluding heap headers and page tables. */
  size_t extent_records, metadata_bytes;
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

/* Prepared, prevalidated destruction. Retain exclusive ownership/exclusion
 * through completion; each step frees <=budget backing pages, then metadata.
 * No allocation or IPI wait after the first mapping effect. Private callers
 * keep their drained root proof; kernel callers retain the structural token. */
struct vm_reclaim {
  struct vm_range *range;
  struct vm_space *space;
};
enum mm_result vm_free_begin(struct vm_space *space, uintptr_t base, size_t bytes,
    struct vm_reclaim *reclaim);
bool vm_free_step(struct vm_reclaim *reclaim, size_t budget);
/* Requires an initialized heap. New spaces share kernel mappings and start
 * with an empty user area. Failure sets *result to NULL. Allocation, queries
 * and mutation require IF=0 and exclusive ownership; no VM operations from
 * fault handlers. The kernel space still asserts BSP ownership for its reused
 * ranges. Published private changes require the process mutation token after
 * acknowledged root departure and borrower drainage, on its recorded owner CPU.
 * Unpublished loading and drained retirement remain BSP-owned in this slice;
 * resumption reloads CR3. No generic remote/live-root mutation is authorized. */
enum mm_result vm_space_create(struct vm_space **result);
/* Private activation requires a process root reservation. Always flushes that
 * CPU's TLB, including shared kernel translations; acknowledge departure only
 * after activating the kernel root on the permanent scheduler stack. */
enum mm_result vm_space_activate(struct vm_space *space);
/* Rejects kernel/active spaces and reservations with caller-owned mappings.
 * Frees VM-owned backing, private page tables and metadata on success.
 * The active check is local: a remote task must return ownership first. */
enum mm_result vm_space_destroy(struct vm_space *space);
/* The same destruction in bounded steps, BSP, IF=0 per call. Retirement
 * performs the checks and frees nothing; on success the caller keeps exclusive
 * ownership and makes no other VM call on the space. Each release step frees
 * at most budget backed pages, range records or page-table entries read, and
 * returns true once the space itself is freed. Between steps the caller may
 * enable interrupts and yield. */
enum mm_result vm_space_retire(struct vm_space *space);
bool vm_space_release_step(struct vm_space *space, size_t budget);

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
/* The same backing in bounded steps. Begin validates and reserves wait tokens
 * without mapping anything. Each step allocates, zeroes and maps at most budget
 * pages, or after a failure unmaps and frees at most budget pages from the end,
 * and returns true once the backing is complete (status MM_OK) or fully
 * unwound (status holds the failure; the reservation is bare). Between steps
 * the caller keeps the space exclusively and may enable interrupts and yield. */
struct vm_range;
struct vm_backing {
  struct vm_range *range;
  uintptr_t base;
  size_t pages, mapped;
  unsigned permissions;
  uint64_t *tokens;
  uint64_t first_token;
  enum mm_result status;
};
enum mm_result vm_back_begin(struct vm_space *space, uintptr_t base, size_t bytes,
                             unsigned permissions, struct vm_backing *backing);
bool vm_back_step(struct vm_space *space, struct vm_backing *backing, size_t budget);
enum mm_result vm_alloc(struct vm_space *space, size_t bytes, size_t alignment,
                        unsigned permissions, uintptr_t *result);
enum mm_result vm_alloc_at(struct vm_space *space, uintptr_t base, size_t bytes,
                           unsigned permissions);
enum mm_result vm_free(struct vm_space *space, uintptr_t base, size_t bytes);

/* Private sparse reservations own only committed extents. Begin validates a
 * whole reservation base and a nonzero page-aligned contained subrange; RELEASE
 * requires offset zero and the full span. Begin clears its output on failure.
 * IF=0 on the private-space owner CPU: retain drained ownership until completion;
 * finish then returns status and frees the operation. Each step
 * handles at most budget backing pages or extent records, with <=256-page
 * token bookkeeping per extent. Withdrawal retains its existing global waiter
 * scan cost. No other operation on this space may interleave across yields.
 * A failed commit completely unwinds only new backing before completing.
 * Empty page tables may remain until space destruction, as with eager backing. */
enum vm_sparse_action { VM_SPARSE_COMMIT, VM_SPARSE_DECOMMIT, VM_SPARSE_RELEASE };
struct vm_sparse_update;
enum mm_result vm_sparse_reserve(struct vm_space *space, size_t bytes,
    size_t alignment, uintptr_t *address);
enum mm_result vm_sparse_begin(struct vm_space *space, uintptr_t base,
    size_t offset, size_t bytes, enum vm_sparse_action action,
    struct vm_sparse_update **result);
bool vm_sparse_step(struct vm_space *space, struct vm_sparse_update *update,
    size_t budget);
enum mm_result vm_sparse_finish(struct vm_sparse_update *update);

/* Map/unmap one page in a bare reservation. Supplied frames remain caller-owned;
 * unmap returns the frame without freeing it. Do not bypass VM's ownership
 * bookkeeping by mutating its spaces through the arch interface. */
enum mm_result vm_map(struct vm_space *space, uintptr_t base, phys_addr_t physical,
                      unsigned permissions);
/* Map a device-owned frame into a bare kernel reservation as RW/NX/uncached.
 * Caller must exclude RAM/cache aliases. Never free MMIO frames through PMM;
 * undo with vm_unmap and release the virtual reservation. Normally before AP
 * startup. ACPI and prepared PCI/GPIO native owners may fill fresh, never-published
 * addresses in an early reserved window once; no late remap or reuse. */
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
/* IF=0. Shared kernel snapshots use only the short mapping-publication guard,
 * not structural mutator admission: unrelated preparation cannot return BUSY.
 * This scalar snapshot is not a mapping lease; caller retains its consumer's
 * lifetime/exclusion proof for any later use. Private queries still require
 * their existing exclusive or stable active-root ownership. */
enum mm_result vm_query(struct vm_space *space, uintptr_t address,
                        struct page_translation *result);

/* Narrow exception to BSP-only queries: IF=0 on a CPU executing this private
 * space, whose mappings must stay stable through the caller's exclusion. Checks
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
