#ifndef KERNEL_VIRTIO_TRANSPORT_H
#define KERNEL_VIRTIO_TRANSPORT_H

#include <kernel/pci.h>
#include <kernel/pci/msix.h>
#include <stddef.h>

#define VIRTIO_VENDOR_ID 0x1af4
#define VIRTIO_PCI_DEVICE_BASE 0x1040
#define VIRTIO_RESET_TIMEOUT_NS UINT64_C(1000000000)
#define VIRTIO_CONFIG_TIMEOUT_NS UINT64_C(1000000000)
#define VIRTIO_COMMON_BYTES 56
#define VIRTIO_FEATURE_WORD_BITS 32
#define VIRTIO_F_VERSION_1 (UINT64_C(1) << 32)
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER 2u
#define VIRTIO_STATUS_DRIVER_OK 4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED 128u

/* Naturally aligned, little-endian registers. Queue addresses are written as
 * low/high 32-bit halves while the queue is disabled, then queue_enable last. */
struct virtio_pci_common {
  uint32_t device_feature_select, device_feature;
  uint32_t driver_feature_select, driver_feature;
  uint16_t config_msix_vector, num_queues;
  uint8_t device_status, config_generation;
  uint16_t queue_select, queue_size, queue_msix_vector, queue_enable;
  uint16_t queue_notify_off;
  uint32_t queue_desc_low, queue_desc_high;
  uint32_t queue_driver_low, queue_driver_high;
  uint32_t queue_device_low, queue_device_high;
};

_Static_assert(offsetof(struct virtio_pci_common, device_status) == 20 &&
               offsetof(struct virtio_pci_common, queue_notify_off) == 30,
               "VirtIO PCI common register layout");
_Static_assert(sizeof(struct virtio_pci_common) == VIRTIO_COMMON_BYTES &&
               offsetof(struct virtio_pci_common, queue_desc_low) == 32 &&
               offsetof(struct virtio_pci_common, queue_device_low) == 48,
               "VirtIO PCI queue address layout");

struct virtio_queue_info {
  uint16_t max_size;
  uintptr_t notify_address;
};

/* Stable storage for the lifetime of the PCI claim. Shared mechanisms only;
 * each driver owns feature policy, queue storage and its worker lifetime. */
struct virtio_pci_transport {
  const char *name;
  struct pci_claim claim;
  struct pci_region common, notify, isr, device;
  struct pci_msix msix;
  uint32_t notify_multiplier;
};

static inline volatile struct virtio_pci_common *virtio_pci_common(
    const struct virtio_pci_transport *pci)
{
  return (volatile struct virtio_pci_common *)pci->common.mapping.address;
}

/* BSP/IF=0 before AP startup. Claims, resets before BAR probing and maps the
 * registers. DMA/INTx stay disabled. Failure releases this boot-only claim.
 * Device configuration size/alignment bound its required prefix; the full
 * advertised device region is mapped for optional negotiated fields. A zero
 * size means the device type has no device-specific configuration to map. */
bool virtio_pci_prepare(struct virtio_pci_transport *pci, struct pci_device *device,
    const struct boot_info *boot, size_t device_bytes, unsigned device_alignment);
/* Program table entry zero and the config route while function/vector masked.
 * The driver assigns its queue routes before enabling delivery. */
bool virtio_pci_prepare_msix(struct virtio_pci_transport *pci, uint8_t vector);
bool virtio_pci_disable_msix(struct virtio_pci_transport *pci);
/* Boot-only bounded reset; runtime workers must sleep while awaiting reset. */
bool virtio_pci_reset(struct virtio_pci_transport *pci);
bool virtio_pci_inspect_queue(struct virtio_pci_transport *pci, unsigned index,
    struct virtio_queue_info *queue);

#endif
