#ifndef KERNEL_NET_CONFIG_H
#define KERNEL_NET_CONFIG_H

#include <abi/net_config.h>
#include <abi/syscall.h>
#include <stdbool.h>

/* User task, IF=0. Copies scalar input into bounded shared storage and waits.
 * No user pointers or caller stack addresses cross to the worker. */
enum call_status net_config_exchange(uint64_t operation,
    const struct net_config_request *request, struct net_config_reply *reply);
/* Sole network worker, IF=1. One bounded pass, no blocking device operations. */
bool net_config_service(void);

#endif
