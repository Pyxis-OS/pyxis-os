#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

/* Owned root and private lower-half tables; kernel subtrees are shared.
 * Do not copy a live space or change its root outside the paging code. */
struct arch_address_space {
  phys_addr_t root;
  uintptr_t release_cursor; /* Paging-owned progress of arch_space_release_step(). */
};

void paging_init(struct boot_info *boot);
/* Before the AP replaces its boot root, on the still-valid boot stack. Does
 * not allocate or access CPU-local data; the kernel image is already mapped. */
void paging_prepare_ap(void);
uintptr_t arch_vm_base(void);
size_t arch_vm_size(void);
/* Kernel addresses outside the VM area, mapped only by heap growth. */
uintptr_t arch_heap_arena_base(void);
size_t arch_heap_arena_size(void);
uintptr_t arch_user_vm_base(void);
size_t arch_user_vm_size(void);
struct arch_address_space *arch_kernel_space(void);
/* Caller supplies zero-initialized storage that stays at a stable address. */
enum mm_result arch_space_create(struct arch_address_space *space);
/* Reports activation on this CPU only. Exclusive ownership is required to
 * mutate/destroy a private space; callers must first retire it on its CPU. */
bool arch_space_active(const struct arch_address_space *space);
enum mm_result arch_space_activate(struct arch_address_space *space);
/* Exclusively owned, inactive private space, IF=0 on the calling CPU. Frees
 * private tables bottom-up, never leaf frames; callers reclaim their backing
 * first and make no other use of the space once release begins. Each call
 * reads at most *budget table entries, deducting what it used, and resumes
 * where the previous call stopped. Returns true once the root is freed and
 * the storage is zeroed. */
bool arch_space_release_step(struct arch_address_space *space, size_t *budget);

/* Kernel mutations are limited to its shared allocation area and the heap
 * arena; other spaces permit lower-half mutations. IF=0. General kernel ranges
 * remain BSP-owned; any-CPU heap growth owns unpublished arena pages. A private
 * space may be mutated on its recorded exclusive owner CPU after process root
 * and borrower drainage, with kernel CR3 active. Walks use local scratch slots.
 * Kernel ranges still used by another CPU must not be unmapped or protected;
 * the separate quiescent kernel-flush protocol does not permit live mutation.
 * Private root departure/reentry flushes CR3 locally, without a shootdown.
 * Callers manage virtual reservations; supplied data frames remain caller-owned.
 * Empty tables are retained for reuse until their space is destroyed;
 * failure may retain zeroed tables but never installs a partial data mapping. */
enum mm_result arch_page_map(struct arch_address_space *space,
                             uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions);
/* MM_OK when no page in the range is mapped, MM_COLLISION when one is.
 * Skips absent tables and reads each present leaf table once. */
enum mm_result arch_range_vacant(const struct arch_address_space *space,
                                 uintptr_t virtual, size_t pages);
/* Maps consecutive pages from virtual to the supplied frames, at most count
 * and never past the end of its leaf table, reporting how many in *mapped.
 * The whole run is checked before any entry is installed. */
enum mm_result arch_page_map_run(struct arch_address_space *space, uintptr_t virtual,
                                 size_t count, const phys_addr_t *physical,
                                 unsigned permissions, size_t *mapped);
/* Supervisor RW/NX/UC mapping in the kernel allocation area. */
enum mm_result arch_page_map_mmio(uintptr_t virtual, phys_addr_t physical);
/* BSP IF=0 before AP startup, caller-proven driver-owned scanout page.
 * No generic RAM use; PAT[5] must remain WC. */
enum mm_result arch_page_map_scanout(uintptr_t virtual, phys_addr_t physical);
/* Display-only fixed WC aperture, BSP/IF=0 before AP startup. A supplied boot
 * framebuffer must start at physical. Its existing leaves remain unchanged;
 * new leaves persist until reboot and never belong to a VM reservation. */
enum mm_result paging_display_aperture(phys_addr_t physical, size_t bytes,
    const struct boot_framebuffer *boot, uintptr_t *address);
/* Reject UC aliases of a successful permanent display aperture, including
 * after its PCI claim is released. Queried extents must not overflow. */
bool paging_display_aperture_overlaps(phys_addr_t physical, size_t bytes);
/* PCI ownership changes, BSP/IF=0 before AP startup, for one ECAM page only. */
void paging_pci_config_writable(uintptr_t virtual, bool writable);
enum mm_result arch_page_unmap(struct arch_address_space *space,
                               uintptr_t virtual, phys_addr_t *physical);
/* Unmaps consecutive mapped pages from virtual, at most count and never past
 * the end of its leaf table, storing their frames in physical. Returns the
 * number unmapped; zero for an invalid or unmapped start. */
size_t arch_page_unmap_run(struct arch_address_space *space, uintptr_t virtual,
                           size_t count, phys_addr_t *physical);
enum mm_result arch_page_protect(struct arch_address_space *space,
                                 uintptr_t virtual, unsigned permissions);
enum mm_result arch_page_query(const struct arch_address_space *space,
                               uintptr_t virtual, struct page_translation *result);
/* IF=0 on the CPU exclusively executing this private space. Checks a lower-half
 * user range through recursive mappings, without scratch slots or allocation.
 * Zero bytes ignores the address but still requires the private root active.
 * Mappings must remain stable through the subsequent access. */
bool arch_user_buffer_accessible(const struct arch_address_space *space,
                                 uintptr_t address, size_t bytes, bool write);
/* Any CPU, including panic and fault paths: no locks, scratch slots or
 * allocation. True when ADDRESS is present in the active root's kernel half;
 * always false before the owned root is active. Says nothing about how long
 * the mapping lasts, so callers read through it only while nothing can unmap it. */
bool arch_kernel_address_mapped(uintptr_t address);
/* Any CPU with IF=0, outside interrupt/fault entry. Uses that CPU's own
 * pre-established scratch slot, no allocations; never exposes a lasting pointer. */
void arch_frame_zero(phys_addr_t physical);
/* Any CPU with IF=0, outside interrupt/fault entry. Maps one frame at the
 * calling CPU's data scratch slot until arch_frame_unmap. In between, the
 * caller may copy to or from the frame, including user copies, but must not
 * zero frames, allocate, or walk or change page tables: those reuse the slots. */
void *arch_frame_map(phys_addr_t physical);
void arch_frame_unmap(void);

#endif
