#ifndef KERNEL_NET_REGISTRY_H
#define KERNEL_NET_REGISTRY_H

#include <kernel/net/interface.h>

/* Worker-owned ordered registry. Contexts and links are retained until reboot.
 * Iteration is worker-only; registered() also permits BSP/IF=0 local enqueue. */
struct net_interface *net_interface_first(void);
bool net_interface_registered(const struct net_interface *interface);
/* Worker, IF=1. Exact configured address, or lo for any supported 127/8 source. */
const struct net_interface *net_interface_for_address(uint32_t address);

#endif
