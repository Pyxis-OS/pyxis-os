#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t block_device_id;
#define BLOCK_DEVICE_ID_NONE UINT32_C(0)

enum block_operation { BLOCK_READ, BLOCK_WRITE, BLOCK_FLUSH };
enum block_result {
  BLOCK_OK,
  BLOCK_PENDING,
  BLOCK_FULL,
  BLOCK_INVALID,
  BLOCK_UNAVAILABLE,
  BLOCK_READ_ONLY,
  BLOCK_WRITE_FAILED,
  BLOCK_IO_ERROR,
  BLOCK_UNSUPPORTED,
  BLOCK_TIMED_OUT,
  BLOCK_BUSY,
};

enum block_preparation {
  BLOCK_DEVICE_ABSENT, BLOCK_DEVICE_READY, BLOCK_DEVICE_UNSUPPORTED,
  BLOCK_DEVICE_SETUP_FAILED,
  BLOCK_DEVICE_AMBIGUOUS, BLOCK_INVENTORY_INCOMPLETE, BLOCK_DEVICE_INVALID,
};

/* BSP/IF=0 after preparation. Inventory IDs and setup results are immutable for
 * the boot, including unsupported/failed candidates. An incomplete inventory
 * enables no device. Out-of-range enumeration returns NONE; bad IDs return
 * INVALID. Unavailable I/O does not establish absent hardware. */
size_t block_device_count(void);
block_device_id block_device_at(size_t index);
bool block_inventory_complete(void);
enum block_preparation block_preparation_result(block_device_id device);

struct block_info {
  uint64_t block_count;
  uint32_t block_size, max_transfer, request_slots;
  bool writable, flush_supported, write_failed;
};

struct block_ticket {
  uint64_t generation;
  uint32_t slot;
  block_device_id device;
};

struct block_completion {
  enum block_result result;
  size_t bytes;
  /* Publication happened: failed writes may have changed storage. A completed
   * write still requires a successful later flush for persistence. */
  bool submitted;
};

/* Stable boot-device IDs, no userspace ABI. Nonblocking calls require BSP,
 * IF=0, outside IRQ/fault entry. All pointers refer to caller-owned kernel
 * storage and are borrowed only for the duration of the call. No allocation.
 * Each ticket has one client; do not collect/abandon it while that client waits.
 * Device limits include queued, active and completed-but-uncollected requests. */
enum block_result block_get_info(block_device_id device, struct block_info *info);
/* READ/WRITE: nonzero logical block count, within geometry and transfer limit.
 * WRITE captures bytes before returning; other operations require write_bytes
 * NULL. FLUSH requires first_block=block_count=0, fences earlier I/O on this
 * device and holds its later I/O until completion. Other dependent/overlapping
 * I/O must be ordered by the client. Rejection changes neither ticket nor device. */
enum block_result block_submit(block_device_id device, enum block_operation operation,
    uint64_t first_block, uint32_t block_count, const void *write_bytes,
    struct block_ticket *ticket);
/* The ticket selects its device, slot and generation for collect/wait/abandon.
 * OK consumes it and fills completion (whose result is the I/O status).
 * A successful read copies exactly completion.bytes, requiring sufficient read
 * capacity. Failure leaves read storage untouched. PENDING/INVALID/BUSY consume
 * nothing. No pointer to a read destination is retained by the device worker. */
enum block_result block_collect(const struct block_ticket *ticket, void *read_bytes,
    size_t read_capacity, struct block_completion *completion);
/* Queued work is canceled before publication. Active work loses its client but
 * keeps its slot/DMA ownership until checked completion or confirmed reset.
 * Abandonment neither recalls published writes nor promises a durability fence. */
enum block_result block_abandon(const struct block_ticket *ticket);
/* BSP kernel task, IF=1, no held locks. Wait for a collectable result; OK means
 * ready, not I/O success. Absolute monotonic deadline; timeout does not cancel
 * the request or consume the ticket. Collect or explicitly abandon afterward. */
enum block_result block_wait(const struct block_ticket *ticket, uint64_t deadline_ns);

#endif
