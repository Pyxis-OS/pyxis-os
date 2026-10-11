#ifndef KERNEL_STORAGE_NVME_H
#define KERNEL_STORAGE_NVME_H

#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/boot.h>

/* BSP/IF=0 before AP startup. All observed controllers remain inventoried;
 * exactly one active namespace per supported controller. */
void nvme_prepare(const struct boot_info *boot);
void nvme_start(void);
size_t nvme_device_count(void);
block_device_id nvme_device_at(size_t index);
void nvme_set_id(size_t index, block_device_id id);
bool nvme_inventory_complete(void);
enum block_preparation nvme_preparation_result(block_device_id device);
enum block_result nvme_get_info(block_device_id device, struct block_info *info);
enum block_result nvme_submit(block_device_id device, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes,
    struct block_ticket *ticket);
enum block_result nvme_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion);
enum block_result nvme_abandon(const struct block_ticket *ticket);
enum block_result nvme_wait(const struct block_ticket *ticket, uint64_t deadline_ns);
/* IRQ only wakes the sole BSP controller worker; it does not inspect DMA. */
void nvme_interrupt(void);
/* ACPI worker/IF=1. Seal, drain and notify normal shutdown. Controllers remain
 * retired even when firmware subsequently fails to power off. */
enum call_status nvme_shutdown(void);

#endif
