#ifndef KERNEL_NET_ECHO_H
#define KERNEL_NET_ECHO_H

#include <abi/echo.h>
#include <abi/syscall.h>
#include <stddef.h>
#include <stdbool.h>

/* User task, IF=0. Captured scalar input and local reply storage only. Sleeps
 * until BSP completion; no caller stack/user pointers are published remotely. */
enum call_status net_echo_exchange(uint32_t destination, uint64_t deadline,
    struct echo_reply *reply);

/* Sole BSP worker, IF=1. Bounded scan starts queued calls and expires sent
 * calls. Returns whether work was done, so the worker yields after a batch. */
bool net_echo_service(void);
/* Worker, IF=0. False when idle; otherwise zero for queued work or the
 * earliest active deadline. UINT64_MAX remains a valid absolute deadline. */
bool net_echo_next_deadline(uint64_t *deadline);

/* Worker-only, validated ICMP reply; borrows bytes only during this call. */
void net_echo_receive(uint32_t source, uint32_t destination, uint16_t identifier,
    uint16_t sequence, const uint8_t *payload, size_t length);

#endif
