#ifndef KERNEL_VIRTIO_NET_H
#define KERNEL_VIRTIO_NET_H

#include <kernel/boot.h>

/* Once on BSP/IF=0 before AP startup. Prepare one modern NIC's PCI resources,
 * features and masked MSI-X route. No queues, DMA or DRIVER_OK yet.
 * Missing/unsupported hardware leaves software networking available. */
void virtio_net_prepare(const struct boot_info *boot);

#endif
