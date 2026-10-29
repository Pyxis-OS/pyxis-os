#ifndef KERNEL_LOG_RING_H
#define KERNEL_LOG_RING_H

#include <abi/log.h>
#include <abi/syscall.h>
#include <stddef.h>

/* Static from the first output byte. Any CPU, caller IF=0. Independent of
 * presentation; fatal writers try once and never wait for a held ring lock.
 * Level is stored when c opens a line and ignored on an open line. */
void log_ring_putc(enum log_level level, char c);
void log_ring_panic_begin(void);
/* Source stamped on each line later characters open: 0 for the kernel, a space's
 * log index otherwise. Caller holds the log lock and restores 0 before releasing it. */
void log_ring_set_source(uint8_t source);
/* Any CPU; preserve IF. Copy into kernel-owned storage under the ring lock;
 * never access user mappings, allocate or log while holding it. A read fills
 * bytes with packed log_record pieces, as the log ABI's READ reply. */
bool log_ring_snapshot(struct log_snapshot *snapshot);
enum call_status log_ring_read(struct log_cursor cursor, struct log_cursor end,
    struct log_read_reply *reply, char *bytes, size_t capacity);

#endif
