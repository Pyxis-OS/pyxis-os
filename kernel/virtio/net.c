#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/log_udp.h>
#include <kernel/net/panic_tx.h>
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

struct virtio_net_controller {
  struct net_panic_gate panic_gate;
  uint32_t controller_id;
  struct virtio_net_controller *next;
  struct virtio_pci_transport pci;
  uint64_t offered_features, accepted_features;
  struct virtio_queue_info rx_info, tx_info;
  uint8_t mac[VIRTIO_NET_MAC_BYTES];
  bool identity_known, link_up, prepared, started, active, stopping;
  struct virtio_net_queue rx, tx;
  uint64_t tx_deadlines[VIRTIO_NET_QUEUE_SIZE];
  uint8_t config_generation;
  bool config_unstable;
  uint64_t config_deadline, config_recheck, reset_deadline, reset_recheck;
  bool dma_disabled, interrupts_disabled;
  const char *stop_reason;
  uint64_t interrupts, received, malformed, transmitted, completed, queue_full;
};

static struct virtio_net_controller *controllers;
static bool inventory_complete;

enum virtio_net_operation {
  NET_OPERATION_TX = 1,
  NET_OPERATION_COMPLETION,
  NET_OPERATION_RX,
  NET_OPERATION_CONFIG,
  NET_OPERATION_ACTIVATE,
  NET_OPERATION_RESET,
  NET_OPERATION_IRQ,
};

static bool enter_network(struct virtio_net_controller *controller,
    enum virtio_net_operation operation, uint64_t *flags)
{
  *flags = cpu_save_interrupts();
  if (!net_panic_gate_enter(&controller->panic_gate, operation)) {
    cpu_restore_interrupts(*flags);
    return false;
  }
  return true;
}

static void leave_network(struct virtio_net_controller *controller, uint64_t flags)
{
  net_panic_gate_leave(&controller->panic_gate);
  cpu_restore_interrupts(flags);
}

static bool refresh_network_config(struct virtio_net_controller *controller, bool force);

static bool sample_network_config(const struct virtio_net_controller *controller,
    uint8_t mac[VIRTIO_NET_MAC_BYTES], bool *link_up, uint8_t *generation)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&controller->pci);
  const volatile struct virtio_net_config *config =
    (const volatile struct virtio_net_config *)controller->pci.device.mapping.address;
  *generation = common->config_generation;
  for (size_t i = 0; i < VIRTIO_NET_MAC_BYTES; ++i) {
    mac[i] = config->mac[i];
  }
  /* Without STATUS, VirtIO specifies that the driver assumes an active link. */
  *link_up = !(controller->accepted_features & VIRTIO_NET_F_STATUS) ||
    (config->status & VIRTIO_NET_S_LINK_UP);
  return common->config_generation == *generation;
}

static bool read_network_config(struct virtio_net_controller *controller)
{
  if ((controller->accepted_features & VIRTIO_NET_F_STATUS) &&
      controller->pci.device.length < sizeof(struct virtio_net_config)) {
    return false;
  }
  uint64_t start = arch_monotonic_ns();
  do {
    uint8_t mac[VIRTIO_NET_MAC_BYTES], generation;
    bool link_up;
    if (sample_network_config(controller, mac, &link_up, &generation)) {
      unsigned nonzero = 0;
      for (size_t i = 0; i < VIRTIO_NET_MAC_BYTES; ++i) {
        nonzero |= mac[i];
      }
      if (!nonzero || (mac[0] & VIRTIO_NET_MAC_GROUP_BIT)) {
        return false;
      }
      memcpy(controller->mac, mac, sizeof(mac));
      controller->identity_known = true;
      controller->link_up = link_up;
      controller->config_generation = generation;
      return true;
    }
  } while (arch_monotonic_ns() - start < VIRTIO_CONFIG_TIMEOUT_NS);
  return false;
}

static const char *negotiate_network(struct virtio_net_controller *controller)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&controller->pci);
  common->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
  common->device_status |= VIRTIO_STATUS_DRIVER;
  common->device_feature_select = 0;
  uint64_t low = common->device_feature;
  common->device_feature_select = 1;
  controller->offered_features = low | ((uint64_t)common->device_feature << VIRTIO_FEATURE_WORD_BITS);
  klog("virtio-net PCI: offered features[63:0]=0x%lx\n", controller->offered_features);

  uint64_t required = VIRTIO_F_VERSION_1 | VIRTIO_NET_F_MAC;
  if ((controller->offered_features & required) != required) {
    return "modern transport and a device-provided MAC are required";
  }
  /* Split queues, physical DMA and complete Ethernet frames. No offloads,
   * merged RX buffers, control queue or multiple queue pairs are negotiated. */
  controller->accepted_features = required | (controller->offered_features & VIRTIO_NET_F_STATUS);
  common->driver_feature_select = 0;
  common->driver_feature = (uint32_t)controller->accepted_features;
  common->driver_feature_select = 1;
  common->driver_feature = controller->accepted_features >> VIRTIO_FEATURE_WORD_BITS;
  common->device_status |= VIRTIO_STATUS_FEATURES_OK;
  unsigned status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK;
  if (common->device_status != status) {
    return "feature negotiation rejected or device needs reset";
  }
  klog("virtio-net PCI: accepted VERSION_1, MAC%s; FEATURES_OK confirmed\n",
       controller->accepted_features & VIRTIO_NET_F_STATUS ? ", STATUS" : "");

  if (!read_network_config(controller)) {
    return "invalid or unstable network configuration";
  }
  klog("virtio-net PCI: MAC=%x:%x:%x:%x:%x:%x link=%s%s\n",
       (unsigned)controller->mac[0], (unsigned)controller->mac[1], (unsigned)controller->mac[2],
       (unsigned)controller->mac[3], (unsigned)controller->mac[4], (unsigned)controller->mac[5],
       controller->link_up ? "up" : "down",
       controller->accepted_features & VIRTIO_NET_F_STATUS ? "" : " (assumed)");

  if (common->num_queues < 2 ||
      !virtio_pci_inspect_queue(&controller->pci, VIRTIO_NET_RX_QUEUE, &controller->rx_info) ||
      !virtio_pci_inspect_queue(&controller->pci, VIRTIO_NET_TX_QUEUE, &controller->tx_info)) {
    return "required RX/TX queue unavailable, enabled or outside notification region";
  }
  if (!virtio_pci_prepare_msix(&controller->pci, APIC_VIRTIO_NET_VECTOR)) {
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

static bool configure_queue(struct virtio_net_controller *controller,
    struct virtio_net_queue *queue)
{
  volatile struct virtio_pci_common *common = virtio_pci_common(&controller->pci);
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

static const char *prepare_queues(struct virtio_net_controller *controller)
{
  enum mm_result result = virtio_net_queue_allocate(&controller->rx, VIRTIO_NET_RX_QUEUE,
      controller->rx_info.max_size, controller->rx_info.notify_address, true);
  if (result == MM_OK) {
    result = virtio_net_queue_allocate(&controller->tx, VIRTIO_NET_TX_QUEUE,
        controller->tx_info.max_size, controller->tx_info.notify_address, false);
  }
  if (result != MM_OK) {
    klog("virtio-net PCI: queue allocation failed (error %u)\n", (unsigned)result);
    return "cannot allocate RX/TX storage";
  }
  if (!configure_queue(controller, &controller->rx) || !configure_queue(controller, &controller->tx)) {
    return "queue configuration rejected";
  }
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    virtio_net_queue_post(&controller->rx, id, VIRTIO_NET_BUFFER_BYTES);
  }
  return NULL;
}

static void prepare_controller(struct virtio_net_controller *controller,
    struct pci_device *device, const struct boot_info *boot)
{
  controller->pci.name = "virtio-net";
  if (!virtio_pci_prepare(&controller->pci, device, boot, VIRTIO_NET_MAC_BYTES, 2)) {
    return;
  }

  const char *failure = negotiate_network(controller);
  if (!failure) {
    failure = prepare_queues(controller);
  }
  if (!failure) {
    controller->prepared = true;
    klog("virtio-net PCI: queues prepared; DMA and DRIVER_OK clear, "
         "awaiting network worker\n");
    return;
  }

  klog("virtio-net PCI: %s; marking FAILED and resetting\n", failure);
  bool interrupts_disabled = virtio_pci_disable_msix(&controller->pci);
  virtio_pci_common(&controller->pci)->device_status |= VIRTIO_STATUS_FAILED;
  bool reset = virtio_pci_reset(&controller->pci);
  if (!reset || !interrupts_disabled) {
    klog("virtio-net PCI: cleanup unconfirmed (reset=%u MSI-X disabled=%u); "
         "claim and mappings retained until reboot, DMA disabled\n",
         (unsigned)reset, (unsigned)interrupts_disabled);
    return;
  }
  virtio_net_queue_release(&controller->tx);
  virtio_net_queue_release(&controller->rx);
  pci_release_device(&controller->pci.claim);
  controller->pci = (struct virtio_pci_transport){.name = "virtio-net"};
}

void virtio_net_prepare(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  inventory_complete = pci_inventory_state() == PCI_INVENTORY_COMPLETE;
  for (size_t index = 0; index < pci_device_count(); ++index) {
    arch_clock_maintain();
    const struct pci_device *device = pci_device_at(index);
    if (device->vendor_id != VIRTIO_VENDOR_ID ||
        device->device_id != VIRTIO_PCI_DEVICE_BASE + VIRTIO_NET_DEVICE_ID) {
      continue;
    }
    struct virtio_net_controller *controller = kmalloc(sizeof(*controller));
    if (!controller) {
      inventory_complete = false;
      klog("virtio-net %x:%x.%u: no memory for controller state\n",
           device->address.bus, device->address.device, device->address.function);
      continue;
    }
    *controller = (struct virtio_net_controller){
      .controller_id = (uint32_t)index + 1, .next = controllers,
    };
    controllers = controller;
    /* The retained inventory exposes read-only records; transport claiming
     * changes ownership on the same boot-lifetime record through the PCI API. */
    prepare_controller(controller, (struct pci_device *)device, boot);
  }
}

struct virtio_net_controller *virtio_net_first(void)
{
  return controllers;
}

struct virtio_net_controller *virtio_net_next(const struct virtio_net_controller *controller)
{
  return controller ? controller->next : NULL;
}

const uint8_t *virtio_net_identity_mac(const struct virtio_net_controller *controller)
{
  return controller && controller->identity_known ? controller->mac : NULL;
}

bool virtio_net_inventory_complete(void)
{
  return inventory_complete;
}

uint32_t virtio_net_controller_id(const struct virtio_net_controller *controller)
{
  return controller->controller_id;
}

bool virtio_net_prepared(const struct virtio_net_controller *controller)
{
  return controller && !net_panic_gate_closed(&controller->panic_gate) &&
    controller->prepared && !controller->stopping;
}

bool virtio_net_carrier(const struct virtio_net_controller *controller, bool *up)
{
  net_worker_assert_context();
  *up = false;
  if (!virtio_net_prepared(controller) ||
      !(controller->accepted_features & VIRTIO_NET_F_STATUS)) {
    return false;
  }
  uint8_t mac[VIRTIO_NET_MAC_BYTES], generation;
  bool sampled_up;
  if (!sample_network_config(controller, mac, &sampled_up, &generation) ||
      memcmp(mac, controller->mac, sizeof(mac))) {
    return false;
  }
  *up = sampled_up;
  return true;
}

void virtio_net_interrupt(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  bool active = false;
  for (struct virtio_net_controller *controller = controllers; controller;
       controller = controller->next) {
    uint64_t flags;
    if (!enter_network(controller, NET_OPERATION_IRQ, &flags)) {
      continue;
    }
    if (controller->active) {
      ++controller->interrupts;
      active = true;
    }
    leave_network(controller, flags);
  }
  if (active) {
    net_worker_notify();
  }
}

static void stop_network(struct virtio_net_controller *controller, const char *reason)
{
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_RESET, &flags)) {
    return;
  }
  controller->active = false;
  controller->prepared = false;
  controller->config_unstable = false;
  controller->stop_reason = reason;
  controller->interrupts_disabled = virtio_pci_disable_msix(&controller->pci);
  struct pci_claim *claim = &controller->pci.claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  controller->dma_disabled = !(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  virtio_pci_common(&controller->pci)->device_status |= VIRTIO_STATUS_FAILED;
  virtio_pci_common(&controller->pci)->device_status = 0;
  controller->stopping = true;
  controller->reset_deadline = task_deadline_after_ms(VIRTIO_RESET_TIMEOUT_NS / UINT64_C(1000000));
  controller->reset_recheck = 0;
  leave_network(controller, flags);
}

static void finish_stop(struct virtio_net_controller *controller)
{
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_RESET, &flags)) {
    return;
  }
  if (!task_deadline_expired(controller->reset_recheck) &&
      !task_deadline_expired(controller->reset_deadline)) {
    leave_network(controller, flags);
    return;
  }
  bool reset = virtio_pci_common(&controller->pci)->device_status == 0;
  if (!reset && !task_deadline_expired(controller->reset_deadline)) {
    controller->reset_recheck = task_deadline_after_ms(VIRTIO_NET_RECHECK_MS);
    leave_network(controller, flags);
    return;
  }
  controller->stopping = false;
  leave_network(controller, flags);
  /* Neither completed DMA nor reset permits unmapping shared kernel storage
   * after AP startup. Keep the claim, rings and buffers until reboot. */
  klog("virtio-net: %s; stopped (reset=%u MSI-X disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", controller->stop_reason, (unsigned)reset,
       (unsigned)controller->interrupts_disabled, (unsigned)controller->dma_disabled);
}

void virtio_net_start(struct virtio_net_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp() && net_worker_available());
  if (!controller || controller->started) {
    return;
  }
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_ACTIVATE, &flags)) {
    return;
  }
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  controller->started = true;
  /* Options are parsed after boot preparation, before worker activation. */
  controller->tx.panic_reserved = net_log_udp_enabled();
  if (!controller->prepared) {
    leave_network(controller, flags);
    return;
  }
  volatile struct virtio_pci_common *common = virtio_pci_common(&controller->pci);
  struct pci_claim *claim = &controller->pci.claim;
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
    ready = pci_msix_enable(&controller->pci.msix);
  }
  controller->active = ready;
  leave_network(controller, flags);
  if (!ready) {
    stop_network(controller, "activation rejected");
    return;
  }
  /* Carrier may change while DRIVER_OK/delivery are off without a generation
   * notification. Activation must publish a fresh configuration sample. */
  if (!refresh_network_config(controller, true) ||
      !enter_network(controller, NET_OPERATION_RX, &flags)) {
    return;
  }
  virtio_net_queue_notify(&controller->rx);
  leave_network(controller, flags);
  klog("virtio-net: RX/TX active, %u buffers per queue, BSP worker owns completions\n",
       VIRTIO_NET_QUEUE_SIZE);
}

static bool refresh_network_config(struct virtio_net_controller *controller, bool force)
{
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_CONFIG, &flags)) {
    return false;
  }
  volatile struct virtio_pci_common *common = virtio_pci_common(&controller->pci);
  if (!force && !controller->config_unstable &&
      common->config_generation == controller->config_generation) {
    leave_network(controller, flags);
    return true;
  }
  if (controller->config_unstable && !task_deadline_expired(controller->config_recheck) &&
      !task_deadline_expired(controller->config_deadline)) {
    leave_network(controller, flags);
    return true;
  }
  uint8_t mac[VIRTIO_NET_MAC_BYTES], generation;
  bool link_up;
  if (!sample_network_config(controller, mac, &link_up, &generation)) {
    if (!controller->config_unstable) {
      controller->config_unstable = true;
      controller->config_deadline = task_deadline_after_ms(VIRTIO_CONFIG_TIMEOUT_NS / UINT64_C(1000000));
    }
    bool expired = task_deadline_expired(controller->config_deadline);
    controller->config_recheck = task_deadline_after_ms(VIRTIO_NET_RECHECK_MS);
    leave_network(controller, flags);
    if (expired) {
      stop_network(controller, "network configuration did not stabilize");
      return false;
    }
    return true;
  }
  if (memcmp(mac, controller->mac, sizeof(mac))) {
    leave_network(controller, flags);
    stop_network(controller, "device MAC changed");
    return false;
  }
  bool changed = link_up != controller->link_up;
  controller->link_up = link_up;
  controller->config_generation = generation;
  controller->config_unstable = false;
  leave_network(controller, flags);
  if (changed) {
    klog("virtio-net: link %s\n", link_up ? "up" : "down");
  }
  return true;
}

bool virtio_net_next_deadline(struct virtio_net_controller *controller, uint64_t *deadline)
{
  if (!controller || net_panic_gate_closed(&controller->panic_gate)) {
    return false;
  }
  if (controller->stopping) {
    *deadline = controller->reset_recheck < controller->reset_deadline ?
      controller->reset_recheck : controller->reset_deadline;
    return true;
  }
  if (!controller->active) {
    return false;
  }
  bool found = controller->config_unstable;
  uint64_t next = controller->config_recheck < controller->config_deadline ?
    controller->config_recheck : controller->config_deadline;
  for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
    if (controller->tx.device_owned[id] && (!found || controller->tx_deadlines[id] < next)) {
      found = true;
      next = controller->tx_deadlines[id];
    }
  }
  if (found) {
    *deadline = next;
  }
  return found;
}

bool virtio_net_service(struct virtio_net_controller *controller)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (!controller || net_panic_gate_closed(&controller->panic_gate)) {
    return false;
  }
  if (controller->stopping) {
    finish_stop(controller);
    return false;
  }
  if (!controller->active) {
    return false;
  }
  if (virtio_pci_common(&controller->pci)->device_status != (VIRTIO_NET_READY | VIRTIO_STATUS_DRIVER_OK)) {
    stop_network(controller, "device needs reset or status changed");
    return false;
  }
  if (!refresh_network_config(controller, false)) {
    return false;
  }

  struct virtio_net_completion completed[VIRTIO_NET_QUEUE_SIZE];
  unsigned tx_count, rx_count;
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_COMPLETION, &flags)) {
    return false;
  }
  bool valid = virtio_net_queue_complete(&controller->tx, completed, &tx_count);
  bool expired = false;
  if (valid) {
    controller->completed += tx_count;
    for (unsigned id = 0; id < VIRTIO_NET_QUEUE_SIZE; ++id) {
      if (controller->tx.device_owned[id] && task_deadline_expired(controller->tx_deadlines[id])) {
        expired = true;
        break;
      }
    }
  }
  leave_network(controller, flags);
  if (!valid || expired) {
    stop_network(controller, valid ? "TX completion timed out" : "invalid TX completion");
    return false;
  }

  if (!enter_network(controller, NET_OPERATION_RX, &flags)) {
    return false;
  }
  valid = virtio_net_queue_complete(&controller->rx, completed, &rx_count);
  leave_network(controller, flags);
  if (!valid) {
    stop_network(controller, "invalid RX completion");
    return false;
  }
  for (unsigned i = 0; i < rx_count; ++i) {
    if (net_panic_gate_closed(&controller->panic_gate)) {
      return false;
    }
    const struct virtio_net_completion *entry = &completed[i];
    const struct virtio_net_header *header = virtio_net_queue_buffer(&controller->rx, entry->id);
    if (entry->length < sizeof(*header) + ETHERNET_HEADER_BYTES ||
        entry->length > sizeof(*header) + ETHERNET_FRAME_MAX ||
        header->gso_type != VIRTIO_NET_GSO_NONE || (header->flags & VIRTIO_NET_HDR_NEEDS_CSUM)) {
      ++controller->malformed;
    } else {
      ++controller->received;
      /* Protocols borrow this frame only until its RX buffer is reposted. */
      net_ethernet_receive((const uint8_t *)(header + 1), entry->length - sizeof(*header));
    }
    if (!enter_network(controller, NET_OPERATION_RX, &flags)) {
      return false;
    }
    virtio_net_queue_post(&controller->rx, entry->id, VIRTIO_NET_BUFFER_BYTES);
    leave_network(controller, flags);
  }
  if (rx_count) {
    if (!enter_network(controller, NET_OPERATION_RX, &flags)) {
      return false;
    }
    virtio_net_queue_notify(&controller->rx);
    leave_network(controller, flags);
  }
  return tx_count == VIRTIO_NET_QUEUE_SIZE || rx_count == VIRTIO_NET_QUEUE_SIZE;
}

enum net_result virtio_net_transmit(struct virtio_net_controller *controller,
    const void *frame, size_t length)
{
  KASSERT(cpu_current() == cpu_bsp());
  if (!frame || length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return NET_INVALID;
  }
  if (!controller) {
    return NET_UNAVAILABLE;
  }
  uint64_t flags;
  if (!enter_network(controller, NET_OPERATION_TX, &flags)) {
    return NET_UNAVAILABLE;
  }
  if (!controller->active || controller->config_unstable || !controller->link_up) {
    leave_network(controller, flags);
    return NET_UNAVAILABLE;
  }
  unsigned capacity = controller->tx.panic_reserved ? VIRTIO_NET_PANIC_DESCRIPTOR :
    VIRTIO_NET_QUEUE_SIZE;
  unsigned id;
  for (id = 0; id < capacity; ++id) {
    if (!controller->tx.device_owned[id]) {
      break;
    }
  }
  if (id == capacity) {
    ++controller->queue_full;
    leave_network(controller, flags);
    return NET_QUEUE_FULL;
  }

  struct virtio_net_header *header = virtio_net_queue_buffer(&controller->tx, id);
  *header = (struct virtio_net_header){0};
  memcpy(header + 1, frame, length);
  controller->tx_deadlines[id] = task_deadline_after_ms(VIRTIO_NET_TX_TIMEOUT_MS);
  virtio_net_queue_post(&controller->tx, id, sizeof(*header) + length);
  virtio_net_queue_notify(&controller->tx);
  ++controller->transmitted;
  leave_network(controller, flags);
  return NET_OK;
}

bool virtio_net_panic_begin(struct virtio_net_controller *controller)
{
  if (!controller) {
    return false;
  }
  unsigned interrupted;
  if (!net_panic_gate_take(&controller->panic_gate, &interrupted) ||
      (interrupted && interrupted != NET_OPERATION_TX)) {
    return false;
  }
  if (!controller->tx.panic_reserved || !controller->active || controller->stopping ||
      controller->config_unstable || !controller->link_up ||
      virtio_pci_common(&controller->pci)->device_status != (VIRTIO_NET_READY | VIRTIO_STATUS_DRIVER_OK)) {
    return false;
  }
  uint8_t mac[VIRTIO_NET_MAC_BYTES], generation;
  bool link_up;
  if (!sample_network_config(controller, mac, &link_up, &generation) || !link_up ||
      memcmp(mac, controller->mac, sizeof(mac))) {
    return false;
  }
  return virtio_net_queue_panic_begin(&controller->tx);
}

bool virtio_net_panic_transmit(struct virtio_net_controller *controller,
    const void *frame, size_t length)
{
  if (!controller || !frame || length < ETHERNET_HEADER_BYTES ||
      length > ETHERNET_FRAME_MAX || !controller->tx.panic_ready ||
      controller->tx.panic_failed) {
    return false;
  }
  struct virtio_net_header *header = virtio_net_queue_panic_buffer(&controller->tx);
  *header = (struct virtio_net_header){0};
  memcpy(header + 1, frame, length);
  return virtio_net_queue_panic_transmit(&controller->tx, sizeof(*header) + length);
}

const uint8_t *virtio_net_mac(const struct virtio_net_controller *controller)
{
  return controller && controller->prepared ? controller->mac : NULL;
}

bool virtio_net_available(const struct virtio_net_controller *controller)
{
  return controller && !net_panic_gate_closed(&controller->panic_gate) &&
    controller->active && !controller->config_unstable && controller->link_up;
}

bool virtio_net_ready(const struct virtio_net_controller *controller)
{
  return controller && !net_panic_gate_closed(&controller->panic_gate) &&
    controller->active && !controller->config_unstable;
}
