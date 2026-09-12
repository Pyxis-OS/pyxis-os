#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H
#include <kernel/boot.h>
#include <kernel/mm/types.h>

struct page_translation {
  phys_addr_t physical; /* Includes the queried byte offset. */
  unsigned permissions; /* Effective permissions across all four levels. */
};

void paging_init(struct boot_info *boot);
uintptr_t arch_vm_base(void);
size_t arch_vm_size(void);

/* Mutations are limited to the allocation area; kernel image/recursive/scratch
 * mappings belong to arch. Calls are serialized by the single CPU, IF=0.
 * Reserve a VM range before mapping caller-owned frames into it. Supplied data
 * frames remain caller-owned. Empty tables are retained for reuse;
 * failure may retain zeroed tables but never installs a partial data mapping. */
enum mm_result arch_page_map(uintptr_t virtual, phys_addr_t physical,
                             unsigned permissions);
enum mm_result arch_page_unmap(uintptr_t virtual, phys_addr_t *physical);
enum mm_result arch_page_protect(uintptr_t virtual, unsigned permissions);
enum mm_result arch_page_query(uintptr_t virtual, struct page_translation *result);
/* Pre-established scratch slot, no allocations; never exposes a lasting pointer. */
void arch_frame_zero(phys_addr_t physical);

#endif
