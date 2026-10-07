#ifndef KERNEL_LOG_RING_H
#define KERNEL_LOG_RING_H

#include <abi/log.h>
#include <abi/syscall.h>
#include <stddef.h>

/* Static from the first output byte. Any CPU, caller IF=0. Independent of
 * presentation; fatal writers try once and never wait for a held ring lock. */
void log_ring_putc(char c);
void log_ring_panic_begin(void);
/* Any CPU; preserve IF. Copy into kernel-owned storage under the ring lock;
 * never access user mappings, allocate or log while holding it. */
bool log_ring_snapshot(struct log_snapshot *snapshot);
enum call_status log_ring_read(struct log_cursor cursor, struct log_cursor end,
    struct log_read_reply *reply, char *text, size_t capacity);

#endif
