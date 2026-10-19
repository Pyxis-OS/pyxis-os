#ifndef KERNEL_FS_NPFS_RAM_H
#define KERNEL_FS_NPFS_RAM_H

#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/mm/types.h>

struct npfs_ram;
/* Sole npfs worker. Sparse backing is retained after admission until reboot. */
enum call_status npfs_ram_create(uint64_t capacity, uint64_t deadline, struct npfs_ram **out);
void npfs_ram_discard(struct npfs_ram *ram);
enum call_status npfs_ram_io(struct npfs_ram *ram, enum block_operation operation,
    uint64_t first, uint32_t count, void *bytes, uint64_t deadline);
uint64_t npfs_ram_capacity(const struct npfs_ram *ram);

#endif
