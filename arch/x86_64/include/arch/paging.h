#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

struct page_translation {
  phys_addr_t physical; /* Includes the queried byte offset. */
  unsigned permissions; /* Effective permissions across all four levels. */
};

/* Owned root and private lower-half tables; kernel subtrees are shared.
 * Do not copy a live space or change its root outside the paging code. */
struct arch_address_space {
  phys_addr_t root;
};

void paging_init(struct boot_info *boot);
uintptr_t arch_vm_base(void);
size_t arch_vm_size(void);
uintptr_t arch_user_vm_base(void);
size_t arch_user_vm_size(void);
struct arch_address_space *arch_kernel_space(void);
enum mm_result arch_space_create(struct arch_address_space *space);
bool arch_space_active(const struct arch_address_space *space);
enum mm_result arch_space_activate(struct arch_address_space *space);
/* Rejects the kernel/active space. Frees private tables, never leaf frames;
 * callers must reclaim their backing first. Clears the root on success. */
enum mm_result arch_space_destroy(struct arch_address_space *space);

/* Kernel mutations are limited to its shared allocation area; other spaces
 * permit lower-half mutations. Calls are serialized by the single CPU, IF=0.
 * Reserve a VM range before mapping caller-owned frames into it. Supplied data
 * frames remain caller-owned. Empty tables are retained for reuse;
 * failure may retain zeroed tables but never installs a partial data mapping. */
enum mm_result arch_page_map(struct arch_address_space *space,
                             uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions);
enum mm_result arch_page_unmap(struct arch_address_space *space,
                               uintptr_t virtual, phys_addr_t *physical);
enum mm_result arch_page_protect(struct arch_address_space *space,
                                 uintptr_t virtual, unsigned permissions);
enum mm_result arch_page_query(const struct arch_address_space *space,
                               uintptr_t virtual, struct page_translation *result);
/* Pre-established scratch slot, no allocations; never exposes a lasting pointer. */
void arch_frame_zero(phys_addr_t physical);

#endif
