#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

/* Owned root and private lower-half tables; kernel subtrees are shared.
 * Do not copy a live space or change its root outside the paging code. */
struct arch_address_space {
  phys_addr_t root;
};

void paging_init(struct boot_info *boot);
/* Before the AP replaces its boot root, on the still-valid boot stack. Does
 * not allocate or access CPU-local data; the kernel image is already mapped. */
void paging_prepare_ap(void);
uintptr_t arch_vm_base(void);
size_t arch_vm_size(void);
uintptr_t arch_user_vm_base(void);
size_t arch_user_vm_size(void);
struct arch_address_space *arch_kernel_space(void);
/* Caller supplies zero-initialized storage that stays at a stable address. */
enum mm_result arch_space_create(struct arch_address_space *space);
/* Reports activation on this CPU only. Exclusive ownership is required to
 * mutate/destroy a private space; callers must first retire it on its CPU. */
bool arch_space_active(const struct arch_address_space *space);
enum mm_result arch_space_activate(struct arch_address_space *space);
/* Rejects the kernel/active space. Frees private tables, never leaf frames;
 * callers must reclaim their backing first. Clears the root on success. */
enum mm_result arch_space_destroy(struct arch_address_space *space);

/* Kernel mutations are limited to its shared allocation area; other spaces
 * permit lower-half mutations. Calls are BSP-only, IF=0; APs only activate
 * task spaces. Walks use the calling CPU's own scratch slots. Kernel ranges
 * in use by another CPU must not be unmapped, remapped or protected. There
 * are no remote TLB shootdowns; task ownership transfers flush CR3 locally.
 * Callers manage virtual reservations; supplied data frames remain caller-owned.
 * Empty tables are retained for reuse until their space is destroyed;
 * failure may retain zeroed tables but never installs a partial data mapping. */
enum mm_result arch_page_map(struct arch_address_space *space,
                             uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions);
/* Supervisor RW/NX/UC mapping in the kernel allocation area. */
enum mm_result arch_page_map_mmio(uintptr_t virtual, phys_addr_t physical);
/* PCI ownership changes, BSP/IF=0 before AP startup, for one ECAM page only. */
void paging_pci_config_writable(uintptr_t virtual, bool writable);
enum mm_result arch_page_unmap(struct arch_address_space *space,
                               uintptr_t virtual, phys_addr_t *physical);
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
/* Any CPU with IF=0, outside interrupt/fault entry. Uses that CPU's own
 * pre-established scratch slot, no allocations; never exposes a lasting pointer. */
void arch_frame_zero(phys_addr_t physical);

#endif
