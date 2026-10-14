#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/log_udp.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "ncm.h"

#define USB_NCM_CLASS_IN 0xa1
#define USB_NCM_CLASS_OUT 0x21
#define USB_NCM_DEVICE_IN 0x80
#define USB_NCM_DEVICE_OUT 0x00
#define USB_NCM_INTERFACE_OUT 0x01
#define USB_NCM_GET_DESCRIPTOR 6
#define USB_NCM_SET_CONFIGURATION 9
#define USB_NCM_SET_INTERFACE 11
#define USB_NCM_DESCRIPTOR_STRING 3
#define USB_NCM_PACKET_FILTER 0x43
#define USB_NCM_GET_PARAMETERS 0x80
#define USB_NCM_GET_FORMAT 0x83
#define USB_NCM_SET_FORMAT 0x84
#define USB_NCM_SET_INPUT_SIZE 0x86
#define USB_NCM_GET_DATAGRAM_SIZE 0x87
#define USB_NCM_SET_DATAGRAM_SIZE 0x88
#define USB_NCM_GET_CRC_MODE 0x89
#define USB_NCM_SET_CRC_MODE 0x8a
#define USB_NCM_FILTER_DIRECTED (1u << 2)
#define USB_NCM_FILTER_BROADCAST (1u << 3)

struct usb_ncm_transport {
  struct usb_ncm_transports *owner;
  struct usb_host_device *device;
  struct usb_ncm_nic *nic;
  struct usb_ncm_function function;
  struct usb_ncm_parameters parameters;
  struct usb_host_interrupt *notification;
  struct usb_ticket tx;
  uint64_t batch_deadline, malformed;
  uint32_t downstream_bps, upstream_bps;
  uint16_t sequence;
  unsigned frame_count, notice_bytes;
  bool ready, known, up, started, lost, tx_active;
  uint8_t control[USB_CONTROL_BYTES];
  uint8_t notice[16];
  uint8_t frames[USB_NCM_TX_FRAMES][ETHERNET_FRAME_MAX];
  size_t frame_bytes[USB_NCM_TX_FRAMES];
  uint8_t receive[USB_NCM_NTB_BYTES], transmit[USB_NCM_NTB_BYTES];
};

struct usb_ncm_transports {
  struct usb_host_controller *host;
  unsigned used;
  struct usb_ncm_transport entries[USB_NETWORK_STREAMS];
};

static uint16_t read16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void write16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = value;
  bytes[1] = value >> 8;
}

static void write32(uint8_t *bytes, uint32_t value)
{
  write16(bytes, value);
  write16(bytes + 2, value >> 16);
}

struct usb_ncm_transports *usb_ncm_transports_prepare(struct usb_host_controller *host)
{
  struct usb_ncm_transports *transports = kmalloc(sizeof(*transports));
  if (transports) {
    memset(transports, 0, sizeof(*transports));
    transports->host = host;
  }
  return transports;
}

void usb_ncm_transports_release(struct usb_ncm_transports *transports)
{
  KASSERT(!transports || !transports->used);
  kfree(transports);
}

static enum usb_result control(struct usb_ncm_transport *transport,
    const struct usb_setup *setup, const void *outbound, void *destination,
    size_t capacity, uint64_t deadline, size_t *actual)
{
  uint64_t limit = task_deadline_after_ms(USB_CONTROL_TIMEOUT_MS);
  if (limit > deadline) {
    limit = deadline;
  }
  struct usb_ticket ticket;
  enum usb_result result = usb_host_control_submit(transport->device, setup, outbound, limit, &ticket);
  if (result != USB_OK) {
    return result;
  }
  result = usb_host_control_wait(transport->device, ticket, limit);
  if (result != USB_OK) {
    usb_host_control_abandon(transport->device, ticket);
    return result;
  }
  struct usb_completion completion;
  result = usb_host_control_take(transport->device, ticket, destination, capacity, &completion);
  if (result != USB_OK) {
    usb_host_control_abandon(transport->device, ticket);
    return result;
  }
  *actual = completion.bytes;
  return completion.result;
}

static enum usb_result class_value(struct usb_ncm_transport *transport,
    uint8_t request, uint16_t value, uint16_t length, bool inbound, uint64_t deadline)
{
  struct usb_setup setup = {
    .request_type = inbound ? USB_NCM_CLASS_IN : USB_NCM_CLASS_OUT,
    .request = request, .value = value,
    .index = transport->function.control_interface, .length = length,
  };
  size_t actual = 0;
  enum usb_result result = control(transport, &setup, inbound ? NULL : transport->control,
      inbound ? transport->control : NULL, inbound ? sizeof(transport->control) : 0,
      deadline, &actual);
  return result != USB_OK ? result : actual != length ? USB_INVALID : USB_OK;
}

static enum usb_result read_mac(struct usb_ncm_transport *transport,
    uint64_t deadline, uint8_t mac[6])
{
  struct usb_setup setup = {
    .request_type = USB_NCM_DEVICE_IN, .request = USB_NCM_GET_DESCRIPTOR,
    .value = USB_NCM_DESCRIPTOR_STRING << 8, .length = 255,
  };
  size_t actual = 0;
  enum usb_result result = control(transport, &setup, NULL, transport->control,
      sizeof(transport->control), deadline, &actual);
  if (result != USB_OK) {
    return result;
  }
  const uint8_t *bytes = transport->control;
  if (actual < 4 || (actual & 1) || bytes[0] != actual || bytes[1] != USB_NCM_DESCRIPTOR_STRING) {
    return USB_INVALID;
  }
  uint16_t language = read16(bytes + 2);
  if (!language) {
    return USB_INVALID;
  }
  setup.value |= transport->function.mac_string;
  setup.index = language;
  setup.length = 26;
  result = control(transport, &setup, NULL, transport->control,
      sizeof(transport->control), deadline, &actual);
  return result != USB_OK ? result : usb_ncm_mac_parse(transport->control, actual, mac) ?
    USB_OK : USB_INVALID;
}

static void publish(struct usb_ncm_transport *transport)
{
  uint64_t flags = cpu_save_interrupts();
  usb_ncm_publish(transport->nic, !transport->lost, transport->ready,
      transport->known, transport->up);
  cpu_restore_interrupts(flags);
}

enum usb_result usb_ncm_setup(struct usb_ncm_transports *transports,
    struct usb_ncm_pool *pool, struct usb_host_device *device,
    const struct usb_ncm_function *function, struct usb_ncm_nic *existing,
    uint64_t deadline, struct usb_ncm_transport **output)
{
  usb_host_assert_worker(transports->host);
  if (!output || !function->selected || function->invalid || function->ambiguous) {
    return USB_UNSUPPORTED;
  }
  *output = NULL;
  if (transports->used == USB_NETWORK_STREAMS) {
    return USB_BUSY;
  }
  struct usb_ncm_transport *transport = &transports->entries[transports->used++];
  transport->owner = transports;
  transport->device = device;
  transport->function = *function;
  transport->nic = existing;
  struct usb_setup configure = {
    .request_type = USB_NCM_DEVICE_OUT, .request = USB_NCM_SET_CONFIGURATION,
    .value = function->configuration,
  };
  size_t actual = 0;
  enum usb_result result = control(transport, &configure, NULL, NULL, 0, deadline, &actual);
  if (result != USB_OK || actual) {
    return result != USB_OK ? result : USB_INVALID;
  }
  uint8_t mac[6];
  result = read_mac(transport, deadline, mac);
  if (result != USB_OK) {
    return result;
  }
  if (existing && memcmp(mac, usb_ncm_identity_mac(existing), sizeof(mac))) {
    return USB_UNSUPPORTED;
  }
  result = class_value(transport, USB_NCM_GET_PARAMETERS, 0, USB_NCM_PARAMETERS_BYTES, true, deadline);
  if (result != USB_OK) {
    return result;
  }
  if (usb_ncm_parameters_parse(&transport->parameters, transport->control, USB_NCM_PARAMETERS_BYTES) !=
      USB_NCM_WIRE_OK) {
    return USB_UNSUPPORTED;
  }
  if (transport->parameters.formats & USB_NCM_FORMAT_NTB32) {
    result = class_value(transport, USB_NCM_GET_FORMAT, 0, 2, true, deadline);
    if (result != USB_OK || read16(transport->control) > 1) {
      return result != USB_OK ? result : USB_INVALID;
    }
    result = class_value(transport, USB_NCM_SET_FORMAT, 0, 0, false, deadline);
    if (result != USB_OK) {
      return result;
    }
  }
  if (function->capabilities & USB_NCM_CAP_CRC_MODE) {
    result = class_value(transport, USB_NCM_GET_CRC_MODE, 0, 2, true, deadline);
    if (result != USB_OK || read16(transport->control) > 1) {
      return result != USB_OK ? result : USB_INVALID;
    }
    result = class_value(transport, USB_NCM_SET_CRC_MODE, 0, 0, false, deadline);
    if (result != USB_OK) {
      return result;
    }
  }
  if (function->capabilities & USB_NCM_CAP_MAX_DATAGRAM) {
    result = class_value(transport, USB_NCM_GET_DATAGRAM_SIZE, 0, 2, true, deadline);
    if (result != USB_OK || read16(transport->control) < ETHERNET_FRAME_MAX ||
        read16(transport->control) > function->max_datagram) {
      return result != USB_OK ? result : USB_UNSUPPORTED;
    }
    write16(transport->control, ETHERNET_FRAME_MAX);
    result = class_value(transport, USB_NCM_SET_DATAGRAM_SIZE, 0, 2, false, deadline);
    if (result != USB_OK) {
      return result;
    }
  }
  memset(transport->control, 0, 8);
  write32(transport->control, transport->parameters.rx_bytes);
  bool extended = (function->capabilities & USB_NCM_CAP_INPUT_DATAGRAMS) != 0;
  if (extended) {
    write16(transport->control + 4, USB_NCM_RX_FRAMES);
  }
  result = class_value(transport, USB_NCM_SET_INPUT_SIZE, 0, extended ? 8 : 4, false, deadline);
  if (result != USB_OK) {
    return result;
  }
  if (function->capabilities & USB_NCM_CAP_PACKET_FILTER) {
    result = class_value(transport, USB_NCM_PACKET_FILTER,
        USB_NCM_FILTER_DIRECTED | USB_NCM_FILTER_BROADCAST, 0, false, deadline);
    if (result != USB_OK) {
      return result;
    }
  }
  /* Preflight the whole MTU under the negotiated constraints before admission. */
  struct usb_ncm_frame full_frame = {.data = transport->frames[0], .bytes = ETHERNET_FRAME_MAX};
  unsigned used;
  size_t bytes;
  if (usb_ncm_ntb16_encode(&transport->parameters, function->out.packet, 0,
      &full_frame, 1, transport->transmit, sizeof(transport->transmit), &used, &bytes) != USB_NCM_WIRE_OK) {
    return USB_UNSUPPORTED;
  }
  size_t notification_bytes = usb_speed_is_enhanced(usb_host_device_speed(device)) ?
    function->notification.bytes_per_interval :
    function->notification.packet * (function->notification.transactions + 1);
  if (notification_bytes < sizeof(transport->notice)) {
    notification_bytes = sizeof(transport->notice);
  }
  if (notification_bytes > USB_NETWORK_INTERRUPT_BYTES) {
    return USB_UNSUPPORTED;
  }
  result = usb_host_configure_interrupt_in(device, &function->notification, notification_bytes,
      USB_INTERRUPT_NETWORK, deadline, &transport->notification);
  if (result == USB_OK) {
    result = usb_host_interrupt_start(transport->notification);
  }
  if (result == USB_OK) {
    result = usb_host_configure_network_bulk(device, &function->in, &function->out,
        transport->parameters.rx_bytes, deadline);
  }
  if (result != USB_OK) {
    return result;
  }
  struct usb_setup alternate = {
    .request_type = USB_NCM_INTERFACE_OUT, .request = USB_NCM_SET_INTERFACE,
    .value = function->data_alternate, .index = function->data_interface,
  };
  result = control(transport, &alternate, NULL, NULL, 0, deadline, &actual);
  if (result != USB_OK || actual) {
    return result != USB_OK ? result : USB_INVALID;
  }
  uint64_t flags = cpu_save_interrupts();
  if (!transport->nic) {
    transport->nic = usb_ncm_admit(pool, mac);
  }
  cpu_restore_interrupts(flags);
  if (!transport->nic) {
    return USB_BUSY;
  }
  publish(transport);
  *output = transport;
  ktrace("usb-ncm: candidate %x:%x:%x:%x:%x:%x, NTB16 RX=%u TX=%u datagrams=%u\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
      transport->parameters.rx_bytes, transport->parameters.tx_bytes,
      transport->parameters.max_tx_datagrams);
  return USB_OK;
}

struct usb_ncm_nic *usb_ncm_transport_nic(const struct usb_ncm_transport *transport)
{
  return transport ? transport->nic : NULL;
}

bool usb_ncm_transport_failed(const struct usb_ncm_transport *transport)
{
  return transport && transport->lost;
}

void usb_ncm_transport_lost(struct usb_ncm_transport *transport, bool retry_pending)
{
  if (!transport) {
    return;
  }
  usb_host_assert_worker(transport->owner->host);
  transport->lost = true;
  transport->ready = transport->known = transport->up = false;
  transport->frame_count = 0;
  transport->batch_deadline = 0;
  uint64_t flags = cpu_save_interrupts();
  usb_ncm_withdraw(transport->nic, retry_pending);
  cpu_restore_interrupts(flags);
}

static void notice(struct usb_ncm_transport *transport, const uint8_t *data, size_t bytes)
{
  for (size_t offset = 0; offset < bytes;) {
    unsigned needed = transport->notice_bytes < 8 ? 8 : 8 + read16(transport->notice + 6);
    if (needed > sizeof(transport->notice)) {
      transport->notice_bytes = 0;
      transport->known = transport->up = false;
      ++transport->malformed;
      return;
    }
    size_t count = needed - transport->notice_bytes;
    if (count > bytes - offset) {
      count = bytes - offset;
    }
    memcpy(transport->notice + transport->notice_bytes, data + offset, count);
    transport->notice_bytes += count;
    offset += count;
    if (transport->notice_bytes < 8) {
      continue;
    }
    struct usb_ncm_notification notification;
    size_t consumed;
    enum usb_ncm_wire_result result = usb_ncm_notification_decode(transport->notice,
        transport->notice_bytes, transport->function.control_interface, &notification, &consumed);
    if (result == USB_NCM_WIRE_MORE) {
      continue;
    }
    transport->notice_bytes = 0;
    if (result != USB_NCM_WIRE_OK) {
      transport->known = transport->up = false;
      ++transport->malformed;
      return;
    }
    if (notification.kind == USB_NCM_CONNECTION) {
      transport->known = true;
      transport->up = notification.connected;
      if (!transport->up) {
        transport->frame_count = 0;
        transport->batch_deadline = 0;
      }
    } else if (notification.kind == USB_NCM_SPEED) {
      transport->downstream_bps = notification.downstream_bps;
      transport->upstream_bps = notification.upstream_bps;
    }
  }
}

void usb_ncm_transport_process(struct usb_ncm_transport *transport)
{
  if (!transport || transport->lost || net_log_udp_panicking()) {
    return;
  }
  usb_host_assert_worker(transport->owner->host);
  if (!usb_host_device_present(transport->device)) {
    usb_ncm_transport_lost(transport, true);
    return;
  }
  for (unsigned i = 0; i < USB_INTERRUPT_COMPLETIONS; ++i) {
    struct usb_interrupt_completion completion;
    enum usb_result result = usb_host_interrupt_take(transport->notification,
        transport->control, sizeof(transport->control), &completion);
    if (result == USB_BUSY) {
      break;
    }
    if (result != USB_OK) {
      usb_ncm_transport_lost(transport, true);
      return;
    }
    notice(transport, transport->control, completion.bytes);
  }
  if (!transport->started && usb_ncm_bound(transport->nic)) {
    if (usb_host_async_bulk_start(transport->device) != USB_OK) {
      usb_ncm_transport_lost(transport, true);
      return;
    }
    transport->ready = transport->started = true;
  }
  publish(transport);
  if (!transport->started) {
    return;
  }
  for (unsigned i = 0; i < USB_ASYNC_BULK_COMPLETIONS; ++i) {
    struct usb_interrupt_completion completion;
    enum usb_result result = usb_host_async_bulk_take(transport->device,
        transport->receive, sizeof(transport->receive), &completion);
    if (result == USB_BUSY) {
      break;
    }
    if (result != USB_OK) {
      usb_ncm_transport_lost(transport, true);
      return;
    }
    struct usb_ncm_ntb ntb;
    if (usb_ncm_ntb16_validate(&transport->parameters, transport->receive,
        completion.bytes, &ntb) != USB_NCM_WIRE_OK) {
      ++transport->malformed;
      continue;
    }
    for (unsigned frame = 0; frame < ntb.count; ++frame) {
      usb_ncm_receive(transport->nic, transport->receive + ntb.frames[frame].offset,
          ntb.frames[frame].bytes);
    }
  }
  if (transport->tx_active) {
    struct usb_completion completion;
    enum usb_result result = usb_host_async_bulk_out_take(transport->device, transport->tx, &completion);
    if (result == USB_BUSY) {
      return;
    }
    if (result != USB_OK || completion.result != USB_OK) {
      usb_ncm_transport_lost(transport, true);
      return;
    }
    transport->tx_active = false;
  }
  if (!transport->known || !transport->up) {
    return;
  }
  while (transport->frame_count < USB_NCM_TX_FRAMES) {
    unsigned index = transport->frame_count;
    if (!usb_ncm_take_transmit(transport->nic, transport->frames[index], ETHERNET_FRAME_MAX,
        &transport->frame_bytes[index])) {
      break;
    }
    if (!transport->frame_count) {
      transport->batch_deadline = task_deadline_after_ms(USB_NETWORK_TX_DELAY_MS);
    }
    ++transport->frame_count;
  }
  if (!transport->frame_count || (transport->frame_count < USB_NCM_TX_FRAMES &&
      !task_deadline_expired(transport->batch_deadline))) {
    return;
  }
  struct usb_ncm_frame frames[USB_NCM_TX_FRAMES];
  for (unsigned i = 0; i < transport->frame_count; ++i) {
    frames[i] = (struct usb_ncm_frame){.data = transport->frames[i], .bytes = transport->frame_bytes[i]};
  }
  unsigned used;
  size_t bytes;
  if (usb_ncm_ntb16_encode(&transport->parameters, transport->function.out.packet,
      transport->sequence, frames, transport->frame_count, transport->transmit,
      sizeof(transport->transmit), &used, &bytes) != USB_NCM_WIRE_OK ||
      usb_host_async_bulk_out_submit(transport->device, transport->transmit, bytes,
        task_deadline_after_ms(USB_CONTROL_TIMEOUT_MS), &transport->tx) != USB_OK) {
    usb_ncm_transport_lost(transport, true);
    return;
  }
  ++transport->sequence;
  transport->tx_active = true;
  transport->frame_count -= used;
  for (unsigned i = 0; i < transport->frame_count; ++i) {
    memcpy(transport->frames[i], transport->frames[i + used], transport->frame_bytes[i + used]);
    transport->frame_bytes[i] = transport->frame_bytes[i + used];
  }
  transport->batch_deadline = 0;
}

bool usb_ncm_transport_deadline(const struct usb_ncm_transport *transport, uint64_t *deadline)
{
  if (!transport || transport->lost || transport->tx_active || !transport->batch_deadline) {
    return false;
  }
  *deadline = transport->batch_deadline;
  return true;
}
