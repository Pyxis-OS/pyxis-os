#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/log_udp.h>
#include <kernel/net/usb_ncm.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include "../usb/host.h"
#include "../usb/ncm_provider.h"

enum frame_state { FRAME_FREE, FRAME_WRITING, FRAME_QUEUED, FRAME_READING };

/* Only QUEUED storage may be invalidated by the other BSP worker. A producer
 * rechecks its epoch after copying; a consumer releases its exclusive slot. */
struct frame_slot {
  enum frame_state state;
  uint64_t order;
  size_t length;
  uint8_t bytes[ETHERNET_FRAME_MAX];
};

struct frame_queue {
  struct frame_slot slots[USB_NCM_FRAME_QUEUE];
  uint64_t next_order;
};

struct usb_ncm_nic {
  struct usb_ncm_nic *next;
  struct usb_ncm_pool *pool;
  uint32_t controller_id;
  uint8_t mac[6];
  bool prepared, ready, carrier_known, link_up, bound;
  bool receive_waiting;
  uint64_t epoch, receive_epoch;
  struct frame_queue receive, transmit;
};

struct usb_ncm_pool {
  struct usb_host_controller *host;
  size_t index, count;
  struct usb_ncm_nic nics[USB_NCM_CANDIDATES];
};

struct discovery_source {
  struct usb_ncm_pool *pool;
  bool finished;
};

static struct discovery_source *sources;
static size_t source_count, pending;
static bool inventory_prepared, inventory_failed;
static struct usb_ncm_nic *first_nic, **last_nic = &first_nic;

static void assert_bsp_disabled(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

static void assert_bsp_enabled(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

bool usb_ncm_inventory_prepare(size_t count)
{
  assert_bsp_disabled();
  KASSERT(!inventory_prepared);
  inventory_prepared = true;
  if (count > SIZE_MAX / sizeof(*sources) ||
      pci_device_count() > UINT32_MAX ||
      count > (UINT32_MAX - pci_device_count()) / USB_NCM_CANDIDATES) {
    inventory_failed = true;
    return false;
  }
  if (count) {
    sources = kmalloc(count * sizeof(*sources));
    if (!sources) {
      inventory_failed = true;
      return false;
    }
    memset(sources, 0, count * sizeof(*sources));
  }
  source_count = pending = count;
  return true;
}

struct usb_ncm_pool *usb_ncm_prepare(struct usb_host_controller *host, size_t index)
{
  assert_bsp_disabled();
  if (!host || index >= source_count || sources[index].pool || sources[index].finished) {
    return NULL;
  }
  struct usb_ncm_pool *pool = kmalloc(sizeof(*pool));
  if (!pool) {
    return NULL;
  }
  memset(pool, 0, sizeof(*pool));
  pool->host = host;
  pool->index = index;
  sources[index].pool = pool;
  return pool;
}

void usb_ncm_release_prepared(struct usb_ncm_pool *pool)
{
  assert_bsp_disabled();
  if (!pool) {
    return;
  }
  KASSERT(!pool->count && sources[pool->index].pool == pool);
  sources[pool->index].pool = NULL;
  kfree(pool);
}

void usb_ncm_discovery_finish_at(size_t index, bool inspected)
{
  assert_bsp_disabled();
  if (index >= source_count || sources[index].finished) {
    return;
  }
  sources[index].finished = true;
  KASSERT(pending);
  --pending;
  inventory_failed |= !inspected;
  net_worker_notify();
}

void usb_ncm_discovery_finish(struct usb_ncm_pool *pool, bool inspected)
{
  assert_bsp_disabled();
  if (pool) {
    usb_ncm_discovery_finish_at(pool->index, inspected);
  }
}

struct usb_ncm_nic *usb_ncm_admit(struct usb_ncm_pool *pool, const uint8_t mac[6])
{
  assert_bsp_disabled();
  if (pool) {
    usb_host_assert_worker(pool->host);
  }
  if (!pool || !mac || !net_ethernet_is_unicast(mac) ||
      pool->count == USB_NCM_CANDIDATES || sources[pool->index].finished) {
    return NULL;
  }
  size_t slot = pool->count++;
  struct usb_ncm_nic *nic = &pool->nics[slot];
  nic->pool = pool;
  nic->epoch = 1;
  nic->controller_id = (uint32_t)(pci_device_count() +
      pool->index * USB_NCM_CANDIDATES + slot + 1);
  memcpy(nic->mac, mac, sizeof(nic->mac));
  *last_nic = nic;
  last_nic = &nic->next;
  net_worker_notify();
  return nic;
}

static void discard_queued(struct frame_queue *queue)
{
  for (unsigned i = 0; i < USB_NCM_FRAME_QUEUE; ++i) {
    if (queue->slots[i].state == FRAME_QUEUED) {
      queue->slots[i].state = FRAME_FREE;
    }
  }
}

static void invalidate_frames(struct usb_ncm_nic *nic)
{
  KASSERT(nic->epoch != UINT64_MAX);
  ++nic->epoch;
  nic->receive_waiting = false;
  discard_queued(&nic->receive);
  discard_queued(&nic->transmit);
}

void usb_ncm_publish(struct usb_ncm_nic *nic,
    bool prepared, bool ready, bool known, bool up)
{
  assert_bsp_disabled();
  KASSERT(nic && (!ready || (prepared && nic->bound)) && (!up || known));
  if ((nic->prepared && !prepared) || (nic->ready && !ready) ||
      (nic->carrier_known && nic->link_up && !(known && up))) {
    invalidate_frames(nic);
  }
  bool changed = nic->prepared != prepared || nic->ready != ready ||
      nic->carrier_known != known || nic->link_up != up;
  nic->prepared = prepared;
  nic->ready = ready;
  nic->carrier_known = known;
  nic->link_up = up;
  if (changed) {
    net_worker_notify();
  }
}

void usb_ncm_withdraw(struct usb_ncm_nic *nic, bool retry_pending)
{
  assert_bsp_disabled();
  if (nic) {
    usb_host_assert_worker(nic->pool->host);
  }
  KASSERT(nic);
  invalidate_frames(nic);
  nic->prepared = retry_pending;
  nic->ready = nic->carrier_known = nic->link_up = false;
  net_worker_notify();
}

bool usb_ncm_inventory_complete(void)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool complete = !pending && !inventory_failed;
  cpu_restore_interrupts(flags);
  return complete;
}

struct usb_ncm_nic *usb_ncm_first(void)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  struct usb_ncm_nic *nic = first_nic;
  cpu_restore_interrupts(flags);
  return nic;
}

struct usb_ncm_nic *usb_ncm_next(const struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  struct usb_ncm_nic *next = nic->next;
  cpu_restore_interrupts(flags);
  return next;
}

uint32_t usb_ncm_controller_id(const struct usb_ncm_nic *nic)
{
  return nic->controller_id;
}

const uint8_t *usb_ncm_identity_mac(const struct usb_ncm_nic *nic)
{
  return nic ? nic->mac : NULL;
}

static bool ready(const struct usb_ncm_nic *nic)
{
  /* A preempted consumer may finish an old delivery, but replacement admission
   * stays closed until that claim is released. Protocol work keeps IF=1. */
  return nic && nic->prepared && nic->bound && nic->ready &&
      (!nic->receive_epoch || nic->receive_epoch == nic->epoch) &&
      !net_log_udp_panicking();
}

static bool available(const struct usb_ncm_nic *nic)
{
  return ready(nic) && nic->carrier_known && nic->link_up;
}

bool usb_ncm_prepared(const struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool prepared = nic && nic->prepared && !net_log_udp_panicking();
  cpu_restore_interrupts(flags);
  return prepared;
}

const uint8_t *usb_ncm_mac(const struct usb_ncm_nic *nic)
{
  return usb_ncm_prepared(nic) ? nic->mac : NULL;
}

bool usb_ncm_carrier(const struct usb_ncm_nic *nic, bool *up)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool known = nic && nic->prepared && nic->carrier_known && !net_log_udp_panicking();
  *up = known && nic->link_up;
  cpu_restore_interrupts(flags);
  return known;
}

bool usb_ncm_ready(const struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool result = ready(nic);
  cpu_restore_interrupts(flags);
  return result;
}

bool usb_ncm_available(const struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool result = available(nic);
  cpu_restore_interrupts(flags);
  return result;
}

void usb_ncm_start(struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  nic->bound = true;
  usb_host_notify(nic->pool->host);
  cpu_restore_interrupts(flags);
  kinfo("usb-ncm: net0 bound to NIC %u; transport-ready=%u carrier-known=%u up=%u\n",
      nic->controller_id, nic->ready, nic->carrier_known, nic->link_up);
}

bool usb_ncm_bound(const struct usb_ncm_nic *nic)
{
  assert_bsp_enabled();
  if (nic) {
    usb_host_assert_worker(nic->pool->host);
  }
  uint64_t flags = cpu_save_interrupts();
  bool result = nic && nic->bound && !net_log_udp_panicking();
  cpu_restore_interrupts(flags);
  return result;
}

static struct frame_slot *reserve_frame(struct frame_queue *queue)
{
  for (unsigned i = 0; i < USB_NCM_FRAME_QUEUE; ++i) {
    struct frame_slot *slot = &queue->slots[i];
    if (slot->state == FRAME_FREE) {
      if (queue->next_order == UINT64_MAX) {
        return NULL;
      }
      slot->state = FRAME_WRITING;
      slot->order = ++queue->next_order;
      return slot;
    }
  }
  return NULL;
}

static struct frame_slot *next_frame(struct frame_queue *queue)
{
  struct frame_slot *first = NULL;
  for (unsigned i = 0; i < USB_NCM_FRAME_QUEUE; ++i) {
    struct frame_slot *slot = &queue->slots[i];
    if ((slot->state == FRAME_WRITING || slot->state == FRAME_QUEUED) &&
        (!first || slot->order < first->order)) {
      first = slot;
    }
  }
  return first && first->state == FRAME_QUEUED ? first : NULL;
}

static enum net_result enqueue_frame(struct usb_ncm_nic *nic, struct frame_queue *queue,
    const void *frame, size_t length, bool transmit, uint64_t expected_epoch)
{
  if (!frame || length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return NET_INVALID;
  }
  uint64_t flags = cpu_save_interrupts();
  if (!available(nic) || (expected_epoch && expected_epoch != nic->epoch)) {
    cpu_restore_interrupts(flags);
    return NET_UNAVAILABLE;
  }
  struct frame_slot *slot = reserve_frame(queue);
  if (!slot) {
    if (!transmit) {
      if (queue->next_order == UINT64_MAX) {
        cpu_restore_interrupts(flags);
        return NET_UNAVAILABLE;
      }
      /* Arm before a consumer can free a slot and notify the producer. */
      nic->receive_waiting = true;
    }
    cpu_restore_interrupts(flags);
    return NET_QUEUE_FULL;
  }
  uint64_t epoch = nic->epoch;
  cpu_restore_interrupts(flags);
  memcpy(slot->bytes, frame, length);
  flags = cpu_save_interrupts();
  if (epoch != nic->epoch || !available(nic)) {
    slot->state = FRAME_FREE;
    cpu_restore_interrupts(flags);
    return NET_UNAVAILABLE;
  }
  slot->length = length;
  slot->state = FRAME_QUEUED;
  if (transmit) {
    usb_host_notify(nic->pool->host);
  } else {
    net_worker_notify();
  }
  cpu_restore_interrupts(flags);
  return NET_OK;
}

enum net_result usb_ncm_transmit(struct usb_ncm_nic *nic,
    const void *frame, size_t length)
{
  net_worker_assert_context();
  return nic ? enqueue_frame(nic, &nic->transmit, frame, length, true, 0) : NET_UNAVAILABLE;
}

uint64_t usb_ncm_receive_epoch(struct usb_ncm_nic *nic)
{
  assert_bsp_enabled();
  if (nic) {
    usb_host_assert_worker(nic->pool->host);
  }
  uint64_t flags = cpu_save_interrupts();
  uint64_t epoch = available(nic) ? nic->epoch : 0;
  cpu_restore_interrupts(flags);
  return epoch;
}

enum net_result usb_ncm_receive(struct usb_ncm_nic *nic, uint64_t epoch,
    const void *frame, size_t length)
{
  assert_bsp_enabled();
  if (nic) {
    usb_host_assert_worker(nic->pool->host);
  }
  return nic && epoch ? enqueue_frame(nic, &nic->receive, frame, length, false, epoch) :
      NET_UNAVAILABLE;
}

bool usb_ncm_take_transmit(struct usb_ncm_nic *nic,
    void *destination, size_t capacity, size_t *length)
{
  assert_bsp_enabled();
  if (nic) {
    usb_host_assert_worker(nic->pool->host);
  }
  if (!length) {
    return false;
  }
  *length = 0;
  uint64_t flags = cpu_save_interrupts();
  struct frame_slot *slot = available(nic) ? next_frame(&nic->transmit) : NULL;
  if (!slot || !destination || capacity < slot->length) {
    cpu_restore_interrupts(flags);
    return false;
  }
  slot->state = FRAME_READING;
  size_t bytes = slot->length;
  uint64_t epoch = nic->epoch;
  cpu_restore_interrupts(flags);
  memcpy(destination, slot->bytes, bytes);
  flags = cpu_save_interrupts();
  slot->state = FRAME_FREE;
  bool accepted = epoch == nic->epoch && available(nic);
  if (accepted) {
    *length = bytes;
  }
  net_worker_notify();
  cpu_restore_interrupts(flags);
  return accepted;
}

bool usb_ncm_service(struct usb_ncm_nic *nic)
{
  net_worker_assert_context();
  for (unsigned count = 0; count < USB_NCM_SERVICE_BUDGET; ++count) {
    uint64_t flags = cpu_save_interrupts();
    struct frame_slot *slot = available(nic) ? next_frame(&nic->receive) : NULL;
    if (!slot) {
      cpu_restore_interrupts(flags);
      return false;
    }
    slot->state = FRAME_READING;
    nic->receive_epoch = nic->epoch;
    cpu_restore_interrupts(flags);
    net_ethernet_receive(slot->bytes, slot->length);
    flags = cpu_save_interrupts();
    bool retired = nic->receive_epoch != nic->epoch;
    slot->state = FRAME_FREE;
    nic->receive_epoch = 0;
    if (nic->receive_waiting) {
      nic->receive_waiting = false;
      usb_host_notify(nic->pool->host);
    }
    if (retired) {
      usb_host_notify(nic->pool->host);
      net_worker_notify();
    }
    cpu_restore_interrupts(flags);
  }
  return true;
}
