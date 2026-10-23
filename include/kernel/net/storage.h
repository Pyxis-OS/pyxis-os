#ifndef KERNEL_NET_STORAGE_H
#define KERNEL_NET_STORAGE_H

#include <stddef.h>

/* Initial shared TCP/UDP storage allowance, not a wire-format limit. */
#define NET_STORAGE_LIMIT_BYTES ((size_t)64 * 1024 * 1024)
/* Transient control/handshake space; stream ingress is reserved separately. */
#define NET_STORAGE_CONTROL_RESERVE_BYTES ((size_t)64 * 1024)

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
