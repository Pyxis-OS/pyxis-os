#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/net/interface.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/virtio/net_queue.h>
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
#define VIRTIO_NET_TX_TIMEOUT_MS 5000
#define VIRTIO_NET_RECHECK_MS 1
#define VIRTIO_NET_HDR_NEEDS_CSUM 1u
#define VIRTIO_NET_GSO_NONE 0
#define ETHERNET_HEADER_BYTES 14
#define ETHERNET_FRAME_MAX (ETHERNET_HEADER_BYTES + 1500)
#define VIRTIO_NET_READY (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK)

/* Modern transport includes num_buffers even without merged receive buffers.
 * With no offloads or hash reporting negotiated, this prefix is twelve bytes. */
struct virtio_net_header {
  uint8_t flags, gso_type;
  uint16_t header_length, gso_size, checksum_start, checksum_offset, num_buffers;
};

_Static_assert(sizeof(struct virtio_net_header) == 12, "modern network header");
_Static_assert(sizeof(struct virtio_net_header) + ETHERNET_FRAME_MAX <= VIRTIO_NET_BUFFER_BYTES,
               "receive buffer holds one complete frame");

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
  bool link_up, prepared, active, stopping;
  struct virtio_net_queue rx, tx;
  uint64_t tx_deadlines[VIRTIO_NET_QUEUE_SIZE];
  uint8_t config_generation;
  bool config_unstable;
  uint64_t config_deadline, config_recheck, reset_deadline, reset_recheck;
  bool dma_disabled, interrupts_disabled;
  const char *stop_reason;
  uint64_t interrupts, received, dropped, malformed, transmitted, completed, queue_full;
} network;

static bool sample_network_config(uint8_t mac[VIRTIO_NET_MAC_BYTES], bool *link_up,
                                  uint8_t *generation)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  const volatile struct virtio_net_config *config =
    (const volatile struct virtio_net_config *)network.pci.device.mapping.address;
  *generation = common->config_generation;
  for (size_t i = 0; i < VIRTIO_NET_MAC_BYTES; ++i) {
    mac[i] = config->mac[i];
  }
  /* Without STATUS, VirtIO specifies that the driver assumes an active link. */
  *link_up = !(network.accepted_features & VIRTIO_NET_F_STATUS) ||
    (config->status & VIRTIO_NET_S_LINK_UP);
  return common->config_generation == *generation;
}

static bool read_network_config(void)
{
  if ((network.accepted_features & VIRTIO_NET_F_STATUS) &&
      network.pci.device.length < sizeof(struct virtio_net_config)) {
    return false;
  }
  uint64_t start = arch_monotonic_ns();
  do {
    if (sample_network_config(network.mac, &network.link_up, &network.config_generation)) {
      unsigned nonzero = 0;
      for (size_t i = 0; i < VIRTIO_NET_MAC_BYTES; ++i) {
        nonzero |= network.mac[i];
      }
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

static bool configure_queue(struct virtio_net_queue *queue)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  common->queue_select = queue->index;
  if (common->queue_enable) {
    return false;
  }
  common->queue_size = VIRTIO_NET_QUEUE_SIZE;
  phys_addr_t descriptors = virtio_net_queue_descriptors(queue);
  phys_addr_t available = virtio_net_queue_available(queue);
  phys_addr_t used = virtio_net_queue_used(queue);
  common->queue_desc_low = (uint32_t)descriptors;
  common->queue_desc_high = descriptors >> 32;
  common->queue_driver_low = (uint32_t)available;
  common->queue_driver_high = available >> 32;
  common->queue_device_low = (uint32_t)used;
  common->queue_device_high = used >> 32;
  if (common->queue_size != VIRTIO_NET_QUEUE_SIZE ||
      common->queue_desc_low != (uint32_t)descriptors || common->queue_desc_high != descriptors >> 32 ||
      common->queue_driver_low != (uint32_t)available || common->queue_driver_high != available >> 32 ||
      common->queue_device_low != (uint32_t)used || common->queue_device_high != used >> 32 ||
      common->queue_msix_vector != VIRTIO_NET_MSIX_ENTRY) {
    return false;
  }
  dma_write_barrier();
  common->queue_enable = 1;
  if (common->queue_enable != 1) {
    return false;
  }
  klog("virtio-net PCI: queue %u size=%u descriptors=0x%lx available=0x%lx used=0x%lx\n",
       (unsigned)queue->index, VIRTIO_NET_QUEUE_SIZE, descriptors, available, used);
  return true;
}

static const char *prepare_queues(void)
{
  enum mm_result result = virtio_net_queue_allocate(&network.rx, VIRTIO_NET_RX_QUEUE,
      network.rx_info.max_size, network.rx_info.notify_address, true);
  if (result == MM_OK) {
    result = virtio_net_queue_allocate(&network.tx, VIRTIO_NET_TX_QUEUE,
        network.tx_info.max_size, network.tx_info.notify_address, false);
  }
  if (result != MM_OK) {
    klog("virtio-net PCI: queue allocation failed (error %u)\n", (unsigned)result);
    return "cannot allocate RX/TX storage";
  }
  if (!configure_queue(&network.rx) || !configure_queue(&network.tx)) {
    return "queue configuration rejected";
  }
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    virtio_net_queue_post(&network.rx, id, VIRTIO_NET_BUFFER_BYTES);
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
    failure = prepare_queues();
  }
  if (!failure) {
    network.prepared = true;
    klog("virtio-net PCI: queues prepared; DMA and DRIVER_OK clear, "
         "awaiting network worker\n");
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
  virtio_net_queue_release(&network.tx);
  virtio_net_queue_release(&network.rx);
  pci_release_device(&network.pci.claim);
  network = (typeof(network)){0};
}

void virtio_net_interrupt(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (network.active) {
    ++network.interrupts;
    net_worker_notify();
  }
}

static void stop_network(const char *reason)
{
  uint64_t flags = cpu_save_interrupts();
  network.active = false;
  network.prepared = false;
  network.config_unstable = false;
  network.stop_reason = reason;
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)network.pci.msix_table.mapping.address;
  table[VIRTIO_NET_MSIX_ENTRY].control |= PCI_MSIX_VECTOR_MASK;
  (void)table[VIRTIO_NET_MSIX_ENTRY].control;
  network.interrupts_disabled = virtio_pci_disable_msix(&network.pci);
  struct pci_claim *claim = &network.pci.claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  network.dma_disabled = !(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  virtio_pci_common(&network.pci)->device_status |= VIRTIO_STATUS_FAILED;
  virtio_pci_common(&network.pci)->device_status = 0;
  cpu_restore_interrupts(flags);

  network.stopping = true;
  network.reset_deadline = task_deadline_after_ms(VIRTIO_RESET_TIMEOUT_NS / UINT64_C(1000000));
  network.reset_recheck = 0;
}

static void finish_stop(void)
{
  if (!task_deadline_expired(network.reset_recheck) &&
      !task_deadline_expired(network.reset_deadline)) {
    return;
  }
  bool reset = virtio_pci_common(&network.pci)->device_status == 0;
  if (!reset && !task_deadline_expired(network.reset_deadline)) {
    network.reset_recheck = task_deadline_after_ms(VIRTIO_NET_RECHECK_MS);
    return;
  }
  network.stopping = false;
  /* Neither completed DMA nor reset permits unmapping shared kernel storage
   * after AP startup. Keep the claim, rings and buffers until reboot. */
  klog("virtio-net: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", network.stop_reason, (unsigned)reset,
       (unsigned)network.interrupts_disabled, (unsigned)network.dma_disabled);
}

void virtio_net_start(void)
{
  KASSERT(cpu_current() == cpu_bsp() && net_worker_available());
  if (!network.prepared) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  struct pci_claim *claim = &network.pci.claim;
  bool ready = common->device_status == VIRTIO_NET_READY;
  if (ready) {
    uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
    pci_write16(claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
    ready = (pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  }
  if (ready) {
    common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    ready = common->device_status == (VIRTIO_NET_READY | VIRTIO_STATUS_DRIVER_OK);
  }
  if (ready) {
    volatile struct pci_msix_entry *table =
      (volatile struct pci_msix_entry *)network.pci.msix_table.mapping.address;
    table[VIRTIO_NET_MSIX_ENTRY].control &= ~PCI_MSIX_VECTOR_MASK;
    ready = !(table[VIRTIO_NET_MSIX_ENTRY].control & PCI_MSIX_VECTOR_MASK);
  }
  if (ready) {
    unsigned offset = network.pci.msix_capability + PCI_MSIX_CONTROL;
    uint16_t control = pci_read16(claim->device->address, offset);
    pci_write16(claim, offset, control & ~PCI_MSIX_FUNCTION_MASK);
    ready = (pci_read16(claim->device->address, offset) &
      (PCI_MSIX_ENABLE | PCI_MSIX_FUNCTION_MASK)) == PCI_MSIX_ENABLE;
  }
  network.active = ready;
  cpu_restore_interrupts(flags);
  if (!ready) {
    stop_network("activation rejected");
    return;
  }
  virtio_net_queue_notify(&network.rx);
  klog("virtio-net: RX/TX active, %u buffers per queue, BSP worker owns completions\n",
       VIRTIO_NET_QUEUE_SIZE);
}

static bool refresh_network_config(void)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&network.pci);
  if (!network.config_unstable && common->config_generation == network.config_generation) {
    return true;
  }
  if (network.config_unstable && !task_deadline_expired(network.config_recheck) &&
      !task_deadline_expired(network.config_deadline)) {
    return true;
  }
  uint8_t mac[VIRTIO_NET_MAC_BYTES], generation;
  bool link_up;
  if (!sample_network_config(mac, &link_up, &generation)) {
    if (!network.config_unstable) {
      network.config_unstable = true;
      network.config_deadline = task_deadline_after_ms(VIRTIO_CONFIG_TIMEOUT_NS / UINT64_C(1000000));
    }
    if (task_deadline_expired(network.config_deadline)) {
      stop_network("network configuration did not stabilize");
      return false;
    }
    network.config_recheck = task_deadline_after_ms(VIRTIO_NET_RECHECK_MS);
    return true;
  }
  if (memcmp(mac, network.mac, sizeof(mac))) {
    stop_network("device MAC changed");
    return false;
  }
  if (link_up != network.link_up) {
    klog("virtio-net: link %s\n", link_up ? "up" : "down");
  }
  network.link_up = link_up;
  network.config_generation = generation;
  network.config_unstable = false;
  return true;
}

bool virtio_net_next_deadline(uint64_t *deadline)
{
  if (network.stopping) {
    *deadline = network.reset_recheck < network.reset_deadline ?
      network.reset_recheck : network.reset_deadline;
    return true;
  }
  if (!network.active) {
    return false;
  }
  bool found = network.config_unstable;
  uint64_t next = network.config_recheck < network.config_deadline ?
    network.config_recheck : network.config_deadline;
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    if (network.tx.device_owned[id] && (!found || network.tx_deadlines[id] < next)) {
      found = true;
      next = network.tx_deadlines[id];
    }
  }
  if (found) {
    *deadline = next;
  }
  return found;
}

bool virtio_net_service(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (network.stopping) {
    finish_stop();
    return false;
  }
  if (!network.active) {
    return false;
  }
  if (virtio_pci_common(&network.pci)->device_status != (VIRTIO_NET_READY | VIRTIO_STATUS_DRIVER_OK)) {
    stop_network("device needs reset or status changed");
    return false;
  }
  if (!refresh_network_config()) {
    return false;
  }

  struct virtio_net_completion completed[VIRTIO_NET_QUEUE_SIZE];
  unsigned tx_count, rx_count;
  if (!virtio_net_queue_complete(&network.tx, completed, &tx_count)) {
    stop_network("invalid TX completion");
    return false;
  }
  network.completed += tx_count;
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    if (network.tx.device_owned[id] && task_deadline_expired(network.tx_deadlines[id])) {
      stop_network("TX completion timed out");
      return false;
    }
  }

  if (!virtio_net_queue_complete(&network.rx, completed, &rx_count)) {
    stop_network("invalid RX completion");
    return false;
  }
  for (unsigned i = 0; i < rx_count; ++i) {
    const struct virtio_net_completion *entry = &completed[i];
    const struct virtio_net_header *header = virtio_net_queue_buffer(&network.rx, entry->id);
    if (entry->length < sizeof(*header) + ETHERNET_HEADER_BYTES ||
        entry->length > sizeof(*header) + ETHERNET_FRAME_MAX ||
        header->gso_type != VIRTIO_NET_GSO_NONE || (header->flags & VIRTIO_NET_HDR_NEEDS_CSUM)) {
      ++network.malformed;
    } else {
      ++network.received;
      /* Ethernet/ARP dispatch arrives in the next slice. Never pass an external
       * frame into the loopback-only IPv4 path or retain a DMA buffer pointer. */
      ++network.dropped;
    }
    virtio_net_queue_post(&network.rx, entry->id, VIRTIO_NET_BUFFER_BYTES);
  }
  if (rx_count) {
    virtio_net_queue_notify(&network.rx);
  }
  return tx_count == VIRTIO_NET_QUEUE_SIZE || rx_count == VIRTIO_NET_QUEUE_SIZE;
}

enum net_result virtio_net_transmit(const void *frame, size_t length)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (!frame || length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return NET_INVALID;
  }
  if (!network.active || network.config_unstable || !network.link_up) {
    return NET_UNAVAILABLE;
  }
  unsigned id;
  for (id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    if (!network.tx.device_owned[id]) {
      break;
    }
  }
  if (id == VIRTIO_NET_QUEUE_SIZE) {
    ++network.queue_full;
    return NET_QUEUE_FULL;
  }

  struct virtio_net_header *header = virtio_net_queue_buffer(&network.tx, id);
  *header = (struct virtio_net_header){0};
  memcpy(header + 1, frame, length);
  network.tx_deadlines[id] = task_deadline_after_ms(VIRTIO_NET_TX_TIMEOUT_MS);
  virtio_net_queue_post(&network.tx, id, sizeof(*header) + length);
  virtio_net_queue_notify(&network.tx);
  ++network.transmitted;
  return NET_OK;
}
