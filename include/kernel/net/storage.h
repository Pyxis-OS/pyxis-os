#ifndef KERNEL_NET_STORAGE_H
#define KERNEL_NET_STORAGE_H

#include <stddef.h>

/* Shared TCP/UDP storage budget: one sixteenth of installed RAM, never less than
 * this floor. A stream charges about 2 MiB of it, so this is a bulk-data share,
 * larger than the 1/512 the small-text budgets (clipboard, packages) take. */
#define NET_STORAGE_MIN_BYTES ((size_t)64 * 1024 * 1024)
#define NET_STORAGE_RAM_DIVISOR 16
/* Transient control/handshake space; stream ingress is reserved separately. */
#define NET_STORAGE_CONTROL_RESERVE_BYTES ((size_t)64 * 1024)

/* Sets the budget from installed RAM; before network start-up, any CPU. */
void net_storage_init(void);
size_t net_storage_limit(void);
/* Network worker: logs, at most once a second, that admissions were refused. */
void net_storage_report(void);

/* Charge existing or separately allocated operation storage. Any CPU, including
 * IF=0. A successful charge must remain until the actual owner releases storage. */
bool net_storage_charge(size_t bytes);
void net_storage_uncharge(size_t bytes);

/* Claim future ingress capacity before publishing a TCP receive window. The
 * owner releases it only after its PCB/pbuf teardown has returned. */
bool net_storage_reserve_receive(size_t bytes);
void net_storage_release_receive(size_t bytes);

/* Includes the private allocation header in the shared allowance. Uses the
 * kernel heap on any CPU outside interrupt/fault entry and queue/pressure/PMM
 * locks; saves/restores IF. A failed allocation retains no charge. */
void *net_storage_alloc(size_t bytes);
/* RX/control only: uses the preclaimed ingress/control allowance. Ordinary
 * records, receive rings and new transmit payloads cannot consume these credits. */
void *net_storage_protocol_alloc(size_t bytes);
void net_storage_free(void *pointer);

#endif
