#ifndef KERNEL_NET_LOG_UDP_H
#define KERNEL_NET_LOG_UDP_H

#include <abi/log.h>
#include <stdbool.h>
#include <stdint.h>

/* BSP boot option, before the network worker runs; immutable afterward. */
void net_log_udp_enable(void);
bool net_log_udp_enabled(void);
bool net_log_udp_panicking(void);
uint32_t net_log_udp_worker_cpu(void);
/* Sole BSP network worker; normal capture follows retained ring history. */
bool net_log_udp_service(void);
bool net_log_udp_next_deadline(uint64_t *deadline);
struct net_interface;
void net_log_udp_address(const struct net_interface *interface, uint32_t address);
/* Any CPU, IF=0; preallocated, bounded and independent of log/worker locks. */
void net_log_udp_panic_begin(void);
/* A fatal line keeps the level of the character that started it. */
void net_log_udp_panic_putc(enum log_level level, char c);

#endif
