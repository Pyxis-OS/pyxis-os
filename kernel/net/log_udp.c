#include <arch/cpu.h>
#include <kernel/log_ring.h>
#include <kernel/memory.h>
#include <kernel/net/driver.h>
#include <kernel/net/log_udp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>
#include <kernel/task.h>
#include <remote/log.h>
#include <stdatomic.h>
#include "wire.h"

#define LOG_UDP_POLL_MS 100
#define LOG_IP_TTL 1
#define LOG_UDP_HEADER_BYTES 8
#define LOG_TEXT_OFFSET (ETHERNET_HEADER_BYTES + IPV4_HEADER_SIZE + \
    LOG_UDP_HEADER_BYTES + LOG_UDP_HEADER_SIZE)
#define LOG_FRAME_BYTES (LOG_TEXT_OFFSET + LOG_UDP_TEXT_MAX)

static atomic_bool enabled;
static atomic_uint_fast64_t sequence, fatal_owner;
static atomic_uint source_address;
static uint32_t worker_cpu;
static uint64_t boot_stamp;

/* The BSP worker and fatal CPU have distinct staging. A panic may interrupt
 * any part of normal framing, ring reads or submission without borrowing it. */
static uint8_t normal_frame[LOG_FRAME_BYTES], fatal_frame[LOG_FRAME_BYTES];
static struct log_cursor normal_cursor, normal_next, fatal_cursor;
static size_t normal_length, fatal_length;
static uint64_t poll_deadline;
static bool fatal_ready;
static atomic_bool fatal_busy, fatal_failed;

void net_log_udp_enable(void)
{
  worker_cpu = cpu_initial_apic_id();
  uint32_t low, high;
  /* Sample only during boot. This is a grouping hint, not an entropy source
   * or a clock used for fatal polling. MAC + stamp can collide across boots. */
  __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
  boot_stamp = (uint64_t)high << 32 | low;
  atomic_store_explicit(&enabled, true, memory_order_release);
}

bool net_log_udp_enabled(void)
{
  return atomic_load_explicit(&enabled, memory_order_acquire);
}

bool net_log_udp_panicking(void)
{
  return atomic_load_explicit(&fatal_owner, memory_order_acquire) != 0;
}

uint32_t net_log_udp_worker_cpu(void)
{
  return worker_cpu;
}

void net_log_udp_address(uint32_t address)
{
  atomic_store_explicit(&source_address, address, memory_order_relaxed);
}

static bool take_sequence(uint64_t *value)
{
  uint_fast64_t next = atomic_load_explicit(&sequence, memory_order_relaxed);
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    if (next == UINT64_MAX) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&sequence, &next, next + 1,
        memory_order_relaxed, memory_order_relaxed)) {
      *value = next;
      return true;
    }
  }
  return false;
}

/* Complete IPv4/UDP limited broadcast without allocation, ARP or lwIP.
 * IPv4 permits a zero UDP checksum; the IP header is checksummed here. */
static size_t frame_header(uint8_t *frame, const uint8_t mac[6], uint8_t flags,
    struct log_cursor cursor, size_t text_length, uint64_t packet_sequence)
{
  memset(frame, 0, LOG_TEXT_OFFSET);
  memcpy(frame, net_ethernet_broadcast, 6);
  memcpy(frame + 6, mac, 6);
  net_write_u16(frame + 12, ETHERNET_TYPE_IPV4);
  uint8_t *ip = frame + ETHERNET_HEADER_BYTES;
  size_t udp_length = LOG_UDP_HEADER_BYTES + LOG_UDP_HEADER_SIZE + text_length;
  ip[0] = 0x45;
  net_write_u16(ip + 2, IPV4_HEADER_SIZE + udp_length);
  ip[8] = LOG_IP_TTL;
  ip[9] = IPV4_PROTOCOL_UDP;
  net_write_u32(ip + 12, atomic_load_explicit(&source_address, memory_order_relaxed));
  net_write_u32(ip + 16, IPV4_LIMITED_BROADCAST);
  net_write_u16(ip + 10, net_checksum(ip, IPV4_HEADER_SIZE));
  uint8_t *udp = ip + IPV4_HEADER_SIZE;
  net_write_u16(udp, LOG_UDP_PORT);
  net_write_u16(udp + 2, LOG_UDP_PORT);
  net_write_u16(udp + 4, udp_length);
  struct log_udp_fragment fragment = {
    .flags = flags | (packet_sequence == 0 ? LOG_UDP_FLAG_START : 0),
    .sequence = packet_sequence,
    .line = cursor.line,
    .offset = cursor.offset,
    .boot = boot_stamp,
    .length = text_length,
  };
  memcpy(fragment.mac, mac, sizeof(fragment.mac));
  log_udp_encode_header(udp + LOG_UDP_HEADER_BYTES, &fragment);
  return LOG_TEXT_OFFSET + text_length;
}

bool net_log_udp_service(void)
{
  if (!net_log_udp_enabled() || net_log_udp_panicking()) {
    return false;
  }
  poll_deadline = task_deadline_after_ms(LOG_UDP_POLL_MS);
  const uint8_t *mac = net_driver_mac();
  if (!mac || !net_driver_available()) {
    return false;
  }
  if (!normal_length) {
    struct log_read_reply reply;
    struct log_cursor end = {UINT64_MAX, UINT64_MAX};
    if (log_ring_read(normal_cursor, end, &reply,
        (char *)normal_frame + LOG_TEXT_OFFSET, LOG_UDP_TEXT_MAX) != CALL_OK || !reply.size) {
      return false;
    }
    uint64_t packet_sequence;
    if (!take_sequence(&packet_sequence)) {
      return false;
    }
    struct log_cursor first = normal_cursor;
    if (reply.dropped_lines) {
      first = (struct log_cursor){first.line + reply.dropped_lines, 0};
    }
    size_t text_length = 0;
    normal_next = first;
    while (text_length < reply.size) {
      char c = (char)normal_frame[LOG_TEXT_OFFSET + text_length++];
      if (c == '\n') {
        ++normal_next.line;
        normal_next.offset = 0;
        break;
      }
      ++normal_next.offset;
    }
    normal_length = frame_header(normal_frame, mac, 0, first, text_length, packet_sequence);
  }
  /* Queue-full retries preserve the same sequence and cursor. A fatal CPU
   * closes the driver's gate before touching its own separate frame. */
  uint8_t *ip = normal_frame + ETHERNET_HEADER_BYTES;
  net_write_u32(ip + 12, atomic_load_explicit(&source_address, memory_order_relaxed));
  net_write_u16(ip + 10, 0);
  net_write_u16(ip + 10, net_checksum(ip, IPV4_HEADER_SIZE));
  if (net_driver_transmit(normal_frame, normal_length) != NET_OK) {
    return false;
  }
  normal_cursor = normal_next;
  normal_length = 0;
  return true;
}

bool net_log_udp_next_deadline(uint64_t *deadline)
{
  if (!net_log_udp_enabled() || net_log_udp_panicking()) {
    return false;
  }
  *deadline = poll_deadline;
  return true;
}

void net_log_udp_panic_begin(void)
{
  if (!net_log_udp_enabled()) {
    return;
  }
  uint_fast64_t owner = (uint64_t)cpu_initial_apic_id() + 1;
  uint_fast64_t expected = 0;
  if (!atomic_compare_exchange_strong_explicit(&fatal_owner, &expected, owner,
      memory_order_acq_rel, memory_order_acquire)) {
    if (expected == owner && atomic_load_explicit(&fatal_busy, memory_order_relaxed)) {
      atomic_store_explicit(&fatal_failed, true, memory_order_relaxed);
    }
    /* Exception reporting enters fatal mode before panic() enters it again.
     * A repeated, nonrecursive entry by the same CPU keeps the claimed path. */
    return;
  }
  atomic_store_explicit(&fatal_busy, true, memory_order_relaxed);
  uint8_t mac[6];
  fatal_ready = net_driver_panic_begin(mac);
  if (fatal_ready) {
    memcpy(fatal_frame + 6, mac, 6);
  }
  atomic_store_explicit(&fatal_busy, false, memory_order_relaxed);
}

void net_log_udp_panic_putc(char c)
{
  uint_fast64_t owner = atomic_load_explicit(&fatal_owner, memory_order_acquire);
  if (!owner || owner != (uint64_t)cpu_initial_apic_id() + 1 || !fatal_ready ||
      atomic_load_explicit(&fatal_failed, memory_order_relaxed)) {
    return;
  }
  if (atomic_exchange_explicit(&fatal_busy, true, memory_order_relaxed)) {
    atomic_store_explicit(&fatal_failed, true, memory_order_relaxed);
    return;
  }
  fatal_frame[LOG_TEXT_OFFSET + fatal_length++] = (uint8_t)c;
  if (c == '\n' || fatal_length == LOG_UDP_TEXT_MAX) {
    uint64_t packet_sequence;
    uint8_t mac[6];
    memcpy(mac, fatal_frame + 6, sizeof(mac));
    if (!take_sequence(&packet_sequence)) {
      atomic_store_explicit(&fatal_failed, true, memory_order_relaxed);
    } else {
      size_t length = frame_header(fatal_frame, mac, LOG_UDP_FLAG_FATAL,
          fatal_cursor, fatal_length, packet_sequence);
      if (!net_driver_panic_transmit(fatal_frame, length)) {
        atomic_store_explicit(&fatal_failed, true, memory_order_relaxed);
      }
    }
    fatal_cursor.offset += fatal_length;
    if (c == '\n') {
      ++fatal_cursor.line;
      fatal_cursor.offset = 0;
    }
    fatal_length = 0;
  }
  atomic_store_explicit(&fatal_busy, false, memory_order_relaxed);
}
