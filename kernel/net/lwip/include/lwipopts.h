#ifndef CAELUM_LWIPOPTS_H
#define CAELUM_LWIPOPTS_H

#include <kernel/net/tcp.h>

/* The BSP network worker owns this IPv4/TCP instance. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 0
#define LWIP_RAW 0
#define LWIP_ICMP 0
#define LWIP_IGMP 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define IP_FORWARD 0
#define IP_FRAG 0
#define IP_REASSEMBLY 0
#define LWIP_NETIF_LOOPBACK 0
#define LWIP_HAVE_LOOPIF 0
#define LWIP_STATS 0
#define IP_OPTIONS_ALLOWED 0
#define LWIP_TCP_PCB_NUM_EXT_ARGS 1
#define LWIP_HOOK_FILENAME "caelum_hooks.h"
#define LWIP_HOOK_IP4_ROUTE_SRC caelum_lwip_route
#define LWIP_HOOK_IP4_INPUT_ACCEPT caelum_lwip_accept
#define LWIP_HOOK_TCP_PCB_ALLOCATED caelum_lwip_pcb_allocated
#define LWIP_HOOK_TCP_OUTPUT caelum_lwip_output
#define LWIP_HOOK_TCP_ISN caelum_lwip_isn

/* Conservative IPv4 send ceiling until path-MTU discovery exists. lwIP also
 * respects the peer MSS and local interface MTU; this is not a path guarantee. */
#define TCP_MSS 536
#define TCP_CALCULATE_EFF_SEND_MSS 1
#define TCP_WND NET_TCP_RECEIVE_BYTES
#define TCP_SND_BUF NET_TCP_SEND_BYTES
#define TCP_OOSEQ_MAX_BYTES NET_TCP_RECEIVE_BYTES
#define TCP_OOSEQ_MAX_PBUFS 16
#define SO_REUSE 0
/* Keep the pinned 60-second MSL explicit: TIME_WAIT lasts two MSLs. */
#define TCP_MSL 60000UL

/* Existing kernel heap with aggregate allocation accounting, not another arena. */
#define MEM_ALIGNMENT 16
#define MEM_CUSTOM_ALLOCATOR 1
#define MEM_CUSTOM_MALLOC caelum_lwip_malloc
#define MEM_CUSTOM_CALLOC caelum_lwip_calloc
#define MEM_CUSTOM_FREE caelum_lwip_free
#define MEMP_MEM_MALLOC 1

#endif
