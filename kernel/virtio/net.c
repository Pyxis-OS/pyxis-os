#include <arch/apic.h>
#include <arch/clock.h>
#include <kernel/log.h>
#include <kernel/virtio/net.h>
#include <kernel/virtio/transport.h>

#define VIRTIO_NET_DEVICE_ID 1
#define VIRTIO_NET_F_MAC (UINT64_C(1) << 5)
#define VIRTIO_NET_F_STATUS (UINT64_C(1) << 16)
#define VIRTIO_NET_S_LINK_UP 1u
#define VIRTIO_NET_MAC_BYTES 6
#define VIRTIO_NET_MAC_GROUP_BIT 1u
#define VIRTIO_NET_RX_QUEUE 0
#define VIRTIO_NET_TX_QUEUE 1
#define VIRTIO_NET_MSIX_ENTRY 0

struct virtio_net_config {
  uint8_t mac[VIRTIO_NET_MAC_BYTES];
  uint16_t status;
};

_Static_assert(offsetof(struct virtio_net_config, status) == VIRTIO_NET_MAC_BYTES &&
               sizeof(struct virtio_net_config) == 8,
               "VirtIO network configuration prefix");

static struct {
  struct virtio_pci_transport pci;
  uint64_t offered_features, accepted_features;
  struct virtio_queue_info rx_info, tx_info;
  uint8_t mac[VIRTIO_NET_MAC_BYTES];
  bool link_up, prepared;
} network;

static bool read_network_config(void)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  const volatile struct virtio_net_config *config =
    (const volatile struct virtio_net_config *)network.pci.device.mapping.address;
  bool has_status = (network.accepted_features & VIRTIO_NET_F_STATUS) != 0;
  if (has_status && network.pci.device.length < sizeof(*config)) {
    return false;
  }

  uint64_t start = arch_monotonic_ns();
  do {
    uint8_t generation = common->config_generation;
    unsigned nonzero = 0;
    for (size_t i = 0; i < VIRTIO_NET_MAC_BYTES; ++i) {
      network.mac[i] = config->mac[i];
      nonzero |= network.mac[i];
    }
    /* Without STATUS, VirtIO specifies that the driver assumes an active link. */
    network.link_up = !has_status || (config->status & VIRTIO_NET_S_LINK_UP);
    if (common->config_generation == generation) {
      return nonzero && !(network.mac[0] & VIRTIO_NET_MAC_GROUP_BIT);
    }
  } while (arch_monotonic_ns() - start < VIRTIO_CONFIG_TIMEOUT_NS);
  return false;
}

static const char *negotiate_network(void)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  common->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
  common->device_status |= VIRTIO_STATUS_DRIVER;
  common->device_feature_select = 0;
  uint64_t low = common->device_feature;
  common->device_feature_select = 1;
  network.offered_features = low | ((uint64_t)common->device_feature << VIRTIO_FEATURE_WORD_BITS);
  klog("virtio-net PCI: offered features[63:0]=0x%lx\n", network.offered_features);

  uint64_t required = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC;
  if ((network.offered_features & required) != required) {
    return "modern transport and a device-provided MAC are required";
  }
  /* Split queues, physical DMA and complete Ethernet frames. No offloads,
   * merged RX buffers, control queue or multiple queue pairs are negotiated. */
  network.accepted_features = required | (network.offered_features & VIRTIO_NET_F_STATUS);
  common->driver_feature_select = 0;
  common->driver_feature = (uint32_t)network.accepted_features;
  common->driver_feature_select = 1;
  common->driver_feature = network.accepted_features >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  unsigned status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != status) {
    return "feature negotiation rejected or device needs reset";
  }
  klog("virtio-net PCI: accepted VERSION_1, MAC%s; FEATURES_OK confirmed\n",
       network.accepted_features & VIRTIO_NET_F_STATUS ? ", STATUS" : "");

  if (!read_network_config()) {
    return "invalid or unstable network configuration";
  }
  klog("virtio-net PCI: MAC=%x:%x:%x:%x:%x:%x link=%s%s\n",
       (unsigned)network.mac[0], (unsigned)network.mac[1], (unsigned)network.mac[2],
       (unsigned)network.mac[3], (unsigned)network.mac[4], (unsigned)network.mac[5],
       network.link_up ? "up" : "down",
       network.accepted_features & VIRTIO_NET_F_STATUS ? "" : " (assumed)");

  if (common->num_queues < 2 ||
      !virtio_pci_inspect_queue(&network.pci, VIRTIO_NET_RX_QUEUE, &network.rx_info) ||
      !virtio_pci_inspect_queue(&network.pci, VIRTIO_NET_TX_QUEUE, &network.tx_info)) {
    return "required RX/TX queue unavailable, enabled or outside notification region";
  }
  if (!virtio_pci_prepare_msix(&network.pci, APIC_VIRTIO_NET_VECTOR)) {
    return "MSI-X routing rejected";
  }
  common->queue_select = VIRTIO_NET_RX_QUEUE;
  common->queue_msix_vector = VIRTIO_NET_MSIX_ENTRY;
  if (common->queue_msix_vector != VIRTIO_NET_MSIX_ENTRY) {
    return "RX interrupt route rejected";
  }
  common->queue_select = VIRTIO_NET_TX_QUEUE;
  common->queue_msix_vector = VIRTIO_NET_MSIX_ENTRY;
  if (common->queue_msix_vector != VIRTIO_NET_MSIX_ENTRY) {
    return "TX interrupt route rejected";
  }
  if (common->device_status != status) {
    return "device status changed during preparation";
  }
  return NULL;
}

void virtio_net_prepare(const struct boot_info *boot)
{
  struct pci_device *device = pci_find_device(VIRTIO_VENDOR_ID,
      VIRTIO_PCI_DEVICE_BASE + VIRTIO_NET_DEVICE_ID);
  if (!device) {
    return;
  }
  network.pci.name = "virtio-net";
  if (!virtio_pci_prepare(&network.pci, device, boot, VIRTIO_NET_MAC_BYTES, 2)) {
    return;
  }

  const char *failure = negotiate_network();
  if (!failure) {
    network.prepared = true;
    klog("virtio-net PCI: prepared; queues disabled, DMA and DRIVER_OK clear, "
         "MSI-X delivery masked\n");
    return;
  }

  klog("virtio-net PCI: %s; marking FAILED and resetting\n", failure);
  bool interrupts_disabled = virtio_pci_disable_msix(&network.pci);
  virtio_pci_common(&network.pci)->device_status |= VIRTIO_STATUS_FAILED;
  bool reset = virtio_pci_reset(&network.pci);
  if (!reset || !interrupts_disabled) {
    klog("virtio-net PCI: cleanup unconfirmed (reset=%u MSI-X disabled=%u); "
         "claim and mappings retained until reboot, DMA disabled\n",
         (unsigned)reset, (unsigned)interrupts_disabled);
    return;
  }
  pci_release_device(&network.pci.claim);
  network = (typeof(network)){0};
}
