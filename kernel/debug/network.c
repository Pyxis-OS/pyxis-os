#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/debug.h>
#include <debug/protocol.h>
#include <kernel/boot/options.h>
#include <kernel/debug.h>
#include <kernel/memory.h>
#include <kernel/net/debug.h>
#include <kernel/net/ipv4.h>
#include <kernel/random.h>
#include <kernel/task.h>
#include <remote/beacon.h>
#include <stdatomic.h>
#include "../net/wire.h"
#include "rsp.h"

#define FRAME_BYTES 1514
#define ETHERNET_BYTES 14
#define UDP_BYTES 8
#define ARP_BYTES 28
#define NS_PER_MS UINT64_C(1000000)
#define RESTORE_MS 1000
#define RELEASE_NOTICE_MS (3 * DEBUG_RETRY_MS)

struct debug_peer {
  uint8_t mac[6];
  uint32_t address;
  uint16_t port;
};

static const struct boot_options *settings DEBUG_DATA;
static uint8_t boot_nonce[DEBUG_NONCE_BYTES] DEBUG_DATA;
static atomic_bool entropy_ready DEBUG_DATA, ready DEBUG_DATA;
static struct net_debug_device candidate DEBUG_DATA;
static struct net_config_reply candidate_ip DEBUG_DATA;
static struct net_config_reply stopped_ip DEBUG_DATA;
static uint8_t local_mac[6] DEBUG_DATA;
static bool waited DEBUG_DATA, bound DEBUG_DATA, closing DEBUG_DATA;
static struct debug_peer peer DEBUG_DATA;
static uint8_t session[DEBUG_NONCE_BYTES] DEBUG_DATA;
static uint64_t generation DEBUG_DATA, received DEBUG_DATA, sent DEBUG_DATA;
static uint64_t idle_since DEBUG_DATA, retry_at DEBUG_DATA;
static uint8_t receive_frame[FRAME_BYTES] DEBUG_DATA;
static uint8_t transmit_frame[FRAME_BYTES] DEBUG_DATA;
static uint8_t pending[DEBUG_DATAGRAM_BYTES] DEBUG_DATA;
static size_t pending_length DEBUG_DATA;
static uint8_t response[DEBUG_PAYLOAD_BYTES] DEBUG_DATA;

static DEBUG_CODE uint64_t milliseconds(void)
{
  arch_clock_maintain();
  return arch_monotonic_ns() / NS_PER_MS;
}

[[noreturn]] static DEBUG_CODE void retained_failure(void)
{
  for (;;) {
    arch_clock_maintain();
    __asm__ volatile("pause");
  }
}

static DEBUG_CODE void prepare_identity(void *argument)
{
  (void)argument;
  uint8_t nonce[DEBUG_NONCE_BYTES] = {0};
  uint64_t flags = cpu_save_interrupts();
  enum call_status status = random_read(nonce, sizeof(nonce),
      task_deadline_after_ms(5000));
  cpu_restore_interrupts(flags);
  if (status == CALL_OK) {
    memcpy(boot_nonce, nonce, sizeof(nonce));
    atomic_store_explicit(&entropy_ready, true, memory_order_release);
  }
  memzero_explicit(nonce, sizeof(nonce));
  flags = cpu_save_interrupts();
  net_worker_notify();
  cpu_restore_interrupts(flags);
}

DEBUG_CODE void debug_prepare(const struct boot_info *boot, const struct boot_options *options)
{
  settings = options;
  net_driver_debug_enable();
  arch_debug_enable(boot);
}

DEBUG_CODE void debug_start(void)
{
  if (settings) {
    /* Failure leaves debugging unavailable; normal networking still runs. */
    (void)kernel_task_create(prepare_identity, NULL);
  }
}

DEBUG_CODE void debug_network_update(bool coherent)
{
  atomic_store_explicit(&ready, false, memory_order_release);
  if (!coherent || !atomic_load_explicit(&entropy_ready, memory_order_acquire)) {
    return;
  }
  struct net_debug_device device;
  struct net_config_reply ip = {0};
  if (!net_driver_debug_ready(&device)) {
    return;
  }
  net_ipv4_snapshot(&ip);
  if (!(ip.flags & NET_CONFIG_ASSIGNED) || !ip.address || !ip.prefix || ip.prefix > 32) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  arch_debug_inspect_transport(&device);
  candidate = device;
  candidate_ip = ip;
  atomic_store_explicit(&ready, true, memory_order_release);
  cpu_restore_interrupts(flags);
}

DEBUG_CODE bool debug_network_ready(void)
{
  return atomic_load_explicit(&ready, memory_order_acquire);
}

DEBUG_CODE void debug_network_checkpoint(void)
{
  if (!waited && settings && settings->debug_wait && debug_network_ready()) {
    waited = true;
    (void)arch_debug_checkpoint();
  }
}

DEBUG_CODE bool debug_stop_retained(void)
{
  return net_driver_debug_retained();
}

DEBUG_CODE bool debug_stop_begin(uint64_t next_generation, bool terminal)
{
  (void)terminal;
  if (!debug_network_ready()) {
    return false;
  }
  enum net_debug_status result = net_driver_debug_begin(next_generation);
  if (result != NET_DEBUG_OK) {
    return false;
  }
  generation = next_generation;
  stopped_ip = candidate_ip;
  memcpy(local_mac, candidate.mac, sizeof(local_mac));
  bound = false;
  closing = false;
  received = sent = 0;
  pending_length = 0;
  memset(session, 0, sizeof(session));
  idle_since = milliseconds();
  debug_rsp_reset();
  return true;
}

static DEBUG_CODE bool neighbor(uint32_t address)
{
  uint32_t mask = UINT32_MAX << (32 - stopped_ip.prefix);
  if (!address || address == stopped_ip.address || (address >> 24) >= 224 ||
      (address >> 24) == 127 || (address & mask) != (stopped_ip.address & mask)) {
    return false;
  }
  return stopped_ip.prefix >= 31 ||
    ((address & ~mask) != 0 && (address & ~mask) != ~mask);
}

static DEBUG_CODE bool same_peer(const struct debug_peer *a, const struct debug_peer *b)
{
  return a->address == b->address && a->port == b->port &&
    !memcmp(a->mac, b->mac, sizeof(a->mac));
}

static DEBUG_CODE bool transmit(const uint8_t *frame, size_t length)
{
  enum net_debug_status status = net_driver_debug_transmit(generation, frame, length);
  if (status == NET_DEBUG_FAILED || status == NET_DEBUG_UNSAFE ||
      status == NET_DEBUG_UNAVAILABLE) {
    retained_failure();
  }
  return status == NET_DEBUG_OK;
}

static DEBUG_CODE bool send_datagram(const struct debug_peer *destination,
                                     const uint8_t *bytes, size_t length)
{
  size_t ip_length = IPV4_HEADER_SIZE + UDP_BYTES + length;
  memcpy(transmit_frame, destination->mac, 6);
  memcpy(transmit_frame + 6, local_mac, 6);
  net_write_u16(transmit_frame + 12, 0x0800);
  uint8_t *ip = transmit_frame + ETHERNET_BYTES;
  memset(ip, 0, IPV4_HEADER_SIZE + UDP_BYTES);
  ip[0] = 0x45;
  net_write_u16(ip + 2, ip_length);
  ip[8] = 64;
  ip[9] = IPV4_PROTOCOL_UDP;
  net_write_u32(ip + 12, stopped_ip.address);
  net_write_u32(ip + 16, destination->address);
  net_write_u16(ip + 10, net_checksum(ip, IPV4_HEADER_SIZE));
  uint8_t *udp = ip + IPV4_HEADER_SIZE;
  net_write_u16(udp, DEBUG_PORT);
  net_write_u16(udp + 2, destination->port);
  net_write_u16(udp + 4, UDP_BYTES + length);
  /* IPv4 permits a zero UDP checksum. Inbound nonzero checksums are verified. */
  memcpy(udp + UDP_BYTES, bytes, length);
  return transmit(transmit_frame, ETHERNET_BYTES + ip_length);
}

static DEBUG_CODE struct debug_packet header(enum debug_packet_kind kind)
{
  struct debug_packet packet = {.kind = kind, .flags = DEBUG_FLAG_TARGET,
    .generation = generation, .ack = received};
  memcpy(packet.boot, boot_nonce, sizeof(packet.boot));
  memcpy(packet.session, session, sizeof(packet.session));
  return packet;
}

static DEBUG_CODE void control(enum debug_packet_kind kind,
                                const struct debug_peer *destination,
                                const uint8_t *nonce)
{
  struct debug_packet packet = header(kind);
  if (nonce) {
    memcpy(packet.session, nonce, sizeof(packet.session));
  }
  /* Discovery/control never consumes the retained sequenced send slot. */
  uint8_t bytes[DEBUG_HEADER_BYTES];
  size_t length = debug_encode(bytes, &packet);
  (void)send_datagram(destination, bytes, length);
}

static DEBUG_CODE void queue(enum debug_packet_kind kind, uint64_t command,
                              const uint8_t *payload, size_t length)
{
  if (pending_length || sent == UINT64_MAX) {
    retained_failure();
  }
  struct debug_packet packet = header(kind);
  packet.sequence = ++sent;
  packet.command = command;
  packet.payload = payload;
  packet.length = length;
  pending_length = debug_encode(pending, &packet);
  retry_at = 0;
}

static DEBUG_CODE void service_send(uint64_t now)
{
  if (pending_length && now >= retry_at && send_datagram(&peer, pending, pending_length)) {
    retry_at = now + DEBUG_RETRY_MS;
  }
}

static DEBUG_CODE void offer(const struct debug_peer *source, const struct debug_packet *request)
{
  size_t name_length = remote_beacon_name_length(settings->debug_net);
  uint8_t payload[DEBUG_OFFER_FIXED_BYTES + DEBUG_NAME_BYTES] = {0};
  memcpy(payload, local_mac, 6);
  debug_put16(payload + 6, arch_debug_stop.cpu_count);
  payload[8] = arch_debug_stop.reason;
  payload[9] = arch_debug_stop.terminal;
  payload[10] = name_length;
  memcpy(payload + 12, settings->debug_image, DEBUG_IMAGE_BYTES);
  memcpy(payload + DEBUG_OFFER_FIXED_BYTES, settings->debug_net, name_length);
  struct debug_packet packet = header(DEBUG_OFFER);
  memcpy(packet.session, request->session, sizeof(packet.session));
  packet.ack = 0;
  packet.payload = payload;
  packet.length = DEBUG_OFFER_FIXED_BYTES + name_length;
  uint8_t bytes[DEBUG_HEADER_BYTES + sizeof(payload)];
  size_t length = debug_encode(bytes, &packet);
  (void)send_datagram(source, bytes, length);
}

static DEBUG_CODE void arp(const uint8_t *frame, size_t length)
{
  if (length < ETHERNET_BYTES + ARP_BYTES) {
    return;
  }
  const uint8_t *a = frame + ETHERNET_BYTES;
  uint32_t source = net_read_u32(a + 14);
  if (net_read_u16(a) != 1 || net_read_u16(a + 2) != 0x0800 ||
      a[4] != 6 || a[5] != 4 || net_read_u16(a + 6) != 1 ||
      net_read_u32(a + 24) != stopped_ip.address || !neighbor(source) ||
      memcmp(frame + 6, a + 8, 6)) {
    return;
  }
  memcpy(transmit_frame, frame + 6, 6);
  memcpy(transmit_frame + 6, local_mac, 6);
  net_write_u16(transmit_frame + 12, 0x0806);
  uint8_t *reply = transmit_frame + ETHERNET_BYTES;
  memcpy(reply, a, ARP_BYTES);
  net_write_u16(reply + 6, 2);
  memcpy(reply + 18, a + 8, 6);
  net_write_u32(reply + 24, source);
  memcpy(reply + 8, local_mac, 6);
  net_write_u32(reply + 14, stopped_ip.address);
  (void)transmit(transmit_frame, ETHERNET_BYTES + ARP_BYTES);
}

static DEBUG_CODE bool parse_frame(size_t length, struct debug_peer *source,
                                   struct debug_packet *packet)
{
  const uint8_t *frame = receive_frame;
  bool broadcast = true;
  for (unsigned i = 0; i < 6; ++i) {
    broadcast &= frame[i] == 255;
  }
  if (length < ETHERNET_BYTES || (frame[6] & 1) ||
      (!broadcast && memcmp(frame, local_mac, 6))) {
    return false;
  }
  if (net_read_u16(frame + 12) == 0x0806) {
    if (!closing) {
      arp(frame, length);
    }
    return false;
  }
  if (net_read_u16(frame + 12) != 0x0800 || length < ETHERNET_BYTES + IPV4_HEADER_SIZE + UDP_BYTES) {
    return false;
  }
  const uint8_t *ip = frame + ETHERNET_BYTES;
  size_t ip_length = net_read_u16(ip + 2);
  uint32_t destination = net_read_u32(ip + 16);
  uint32_t mask = UINT32_MAX << (32 - stopped_ip.prefix);
  uint32_t subnet_broadcast = stopped_ip.address | ~mask;
  if (ip[0] != 0x45 || ip[9] != IPV4_PROTOCOL_UDP ||
      (net_read_u16(ip + 6) & 0xbfff) || ip_length < IPV4_HEADER_SIZE + UDP_BYTES ||
      ip_length > length - ETHERNET_BYTES || net_checksum(ip, IPV4_HEADER_SIZE) ||
      (destination != stopped_ip.address && destination != UINT32_MAX &&
       !(stopped_ip.prefix < 31 && destination == subnet_broadcast))) {
    return false;
  }
  source->address = net_read_u32(ip + 12);
  if (!neighbor(source->address)) {
    return false;
  }
  const uint8_t *udp = ip + IPV4_HEADER_SIZE;
  size_t udp_length = net_read_u16(udp + 4);
  if (net_read_u16(udp + 2) != DEBUG_PORT || !net_read_u16(udp) ||
      udp_length != ip_length - IPV4_HEADER_SIZE ||
      udp_length > UDP_BYTES + DEBUG_DATAGRAM_BYTES) {
    return false;
  }
  if (net_read_u16(udp + 6)) {
    uint32_t sum = (source->address >> 16) + (source->address & UINT16_MAX) +
      (destination >> 16) + (destination & UINT16_MAX) + IPV4_PROTOCOL_UDP + udp_length;
    for (size_t i = 0; i + 1 < udp_length; i += 2) {
      sum += net_read_u16(udp + i);
    }
    if (udp_length & 1) {
      sum += (uint16_t)udp[udp_length - 1] << 8;
    }
    while (sum >> 16) {
      sum = (sum & UINT16_MAX) + (sum >> 16);
    }
    if ((uint16_t)sum != UINT16_MAX) {
      return false;
    }
  }
  memcpy(source->mac, frame + 6, 6);
  source->port = net_read_u16(udp);
  return debug_decode(udp + UDP_BYTES, udp_length - UDP_BYTES, packet);
}

static DEBUG_CODE bool message(const struct debug_peer *source,
                                const struct debug_packet *packet,
                                uint64_t now, uint64_t *continue_command)
{
  if (packet->flags & DEBUG_FLAG_TARGET) {
    return false;
  }
  if (packet->kind == DEBUG_HELLO) {
    if (closing) {
      return false;
    }
    size_t name_length = remote_beacon_name_length(settings->debug_net);
    if (packet->length == DEBUG_HELLO_FIXED_BYTES + name_length &&
        packet->payload[32] == name_length && !packet->payload[33] &&
        !packet->payload[34] && !packet->payload[35] &&
        !memcmp(packet->payload + DEBUG_HELLO_FIXED_BYTES, settings->debug_net, name_length) &&
        !packet->sequence && !packet->ack) {
      offer(source, packet);
    }
    return false;
  }
  if (memcmp(packet->boot, boot_nonce, sizeof(boot_nonce)) || packet->generation != generation) {
    return false;
  }
  if (!closing && packet->kind == DEBUG_BIND && (packet->flags & DEBUG_FLAG_ATTACHED) &&
      !packet->sequence && !packet->ack && packet->length == DEBUG_IMAGE_BYTES &&
      !memcmp(packet->payload, settings->debug_image, DEBUG_IMAGE_BYTES)) {
    if (!bound) {
      bool nonzero = false;
      for (size_t i = 0; i < sizeof(session); ++i) {
        nonzero |= packet->session[i] != 0;
      }
      if (!nonzero || ((packet->flags & DEBUG_FLAG_RUNNING) && !packet->command)) {
        return false;
      }
      peer = *source;
      memcpy(session, packet->session, sizeof(session));
      bound = true;
      idle_since = now;
      control(DEBUG_BOUND, &peer, NULL);
      if (packet->flags & DEBUG_FLAG_RUNNING) {
        size_t bytes = 0;
        if (debug_rsp_stop_reply(response, sizeof(response), &bytes) && bytes) {
          queue(DEBUG_PACKET_DATA, packet->command, response, bytes);
        }
      }
      return false;
    }
    if (same_peer(source, &peer) && !memcmp(packet->session, session, sizeof(session))) {
      /* A lost BOUND cannot reset either sequence or command cache. */
      idle_since = now;
      control(DEBUG_BOUND, &peer, NULL);
    }
    return false;
  }
  if (!bound || !same_peer(source, &peer) ||
      memcmp(packet->session, session, sizeof(session)) ||
      packet->flags != DEBUG_FLAG_ATTACHED || packet->ack > sent) {
    return false;
  }
  if ((packet->kind == DEBUG_ACK || packet->kind == DEBUG_HEARTBEAT) &&
      (packet->length || packet->sequence)) {
    return false;
  }
  if (packet->kind != DEBUG_PACKET_DATA && packet->kind != DEBUG_ACK && packet->kind != DEBUG_HEARTBEAT) {
    return false;
  }
  if (packet->kind == DEBUG_PACKET_DATA && (!packet->sequence || !packet->length ||
      !debug_rsp_valid(packet->payload, packet->length))) {
    return false;
  }
  size_t retained_length = pending_length;
  if (pending_length && packet->ack == sent) {
    pending_length = 0;
  }
  if (packet->kind == DEBUG_HEARTBEAT) {
    idle_since = now;
    control(DEBUG_HEARTBEAT, &peer, NULL);
    return false;
  }
  if (packet->kind == DEBUG_ACK) {
    idle_since = now;
    return false;
  }
  if (packet->sequence <= received) {
    idle_since = now;
    control(DEBUG_ACK, &peer, NULL);
    return false;
  }
  if (closing || received == UINT64_MAX || packet->sequence != received + 1 || pending_length) {
    control(DEBUG_ACK, &peer, NULL);
    return false;
  }
  size_t bytes = 0;
  bool resume = false;
  if (!debug_rsp_handle(packet->command, packet->payload, packet->length,
        response, sizeof(response), &bytes, &resume)) {
    pending_length = retained_length;
    return false;
  }
  idle_since = now;
  received = packet->sequence;
  control(DEBUG_ACK, &peer, NULL);
  if (bytes) {
    queue(DEBUG_PACKET_DATA, packet->command, response, bytes);
  }
  if (resume && !arch_debug_stop.terminal) {
    *continue_command = packet->command;
    return true;
  }
  return false;
}

static DEBUG_CODE void poll(uint64_t now, bool accept_commands,
                            bool *resume, uint64_t *command)
{
  size_t length = 0;
  enum net_debug_status status = net_driver_debug_poll(generation,
      receive_frame, sizeof(receive_frame), &length);
  if (status == NET_DEBUG_FAILED || status == NET_DEBUG_UNSAFE ||
      status == NET_DEBUG_UNAVAILABLE) {
    retained_failure();
  }
  if (status == NET_DEBUG_OK && length) {
    struct debug_peer source;
    struct debug_packet packet;
    if (parse_frame(length, &source, &packet) && accept_commands &&
        message(&source, &packet, now, command)) {
      *resume = true;
    }
  }
}

static DEBUG_CODE void release(enum debug_release_reason reason, uint64_t command)
{
  closing = true;
  uint64_t deadline = milliseconds() + RESTORE_MS;
  /* Give the continue acknowledgement its bounded delivery opportunity first. */
  while (pending_length && milliseconds() < deadline) {
    bool ignored = false;
    uint64_t ignored_command = 0;
    uint64_t now = milliseconds();
    poll(now, true, &ignored, &ignored_command);
    service_send(now);
  }
  pending_length = 0;
  if (bound) {
    uint8_t payload = reason;
    queue(DEBUG_RELEASED, command, &payload, sizeof(payload));
    deadline = milliseconds() + RELEASE_NOTICE_MS;
    while (pending_length && milliseconds() < deadline) {
      bool ignored = false;
      uint64_t ignored_command = 0;
      uint64_t now = milliseconds();
      poll(now, true, &ignored, &ignored_command);
      service_send(now);
    }
  }
  /* Peer traffic cannot keep an idle release parked forever. Hardware ownership
   * remains authoritative: only confirmed TX/mask restoration permits resume. */
  deadline = milliseconds() + RESTORE_MS;
  for (;;) {
    enum net_debug_status status = net_driver_debug_restore(generation);
    if (status == NET_DEBUG_OK) {
      break;
    }
    if (status != NET_DEBUG_PENDING || milliseconds() >= deadline) {
      retained_failure();
    }
    bool ignored = false;
    uint64_t ignored_command = 0;
    poll(milliseconds(), false, &ignored, &ignored_command);
  }
  pending_length = 0;
}

DEBUG_CODE void debug_stop_run(uint64_t stop_generation)
{
  if (stop_generation != generation || !debug_stop_retained()) {
    retained_failure();
  }
  /* Incomplete acquisition time is excluded from the ready-stop idle budget. */
  idle_since = milliseconds();
  for (;;) {
    uint64_t now = milliseconds();
    bool resume = false;
    uint64_t command = 0;
    poll(now, true, &resume, &command);
    service_send(now);
    if (resume) {
      release(DEBUG_RELEASE_CONTINUE, command);
      return;
    }
    if (!arch_debug_stop.terminal && now - idle_since >= DEBUG_IDLE_MS) {
      release(DEBUG_RELEASE_IDLE, 0);
      return;
    }
    __asm__ volatile("pause");
  }
}
