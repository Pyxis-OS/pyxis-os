#ifndef DEBUG_PROTOCOL_H
#define DEBUG_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_PORT 2326
#define DEBUG_PROTOCOL_VERSION 1
#define DEBUG_HEADER_BYTES 80
#define DEBUG_PAYLOAD_BYTES 1024
#define DEBUG_DATAGRAM_BYTES (DEBUG_HEADER_BYTES + DEBUG_PAYLOAD_BYTES)
#define DEBUG_NONCE_BYTES 16
#define DEBUG_IMAGE_BYTES 32
#define DEBUG_NAME_BYTES 63
#define DEBUG_HELLO_FIXED_BYTES 36
#define DEBUG_OFFER_FIXED_BYTES 44
#define DEBUG_FLAG_TARGET 1u
#define DEBUG_FLAG_ATTACHED 2u
#define DEBUG_FLAG_RUNNING 4u
#define DEBUG_RETRY_MS 250
#define DEBUG_HEARTBEAT_MS 2000
#define DEBUG_IDLE_MS 30000

enum debug_packet_kind {
  DEBUG_HELLO = 1,
  DEBUG_OFFER,
  DEBUG_BIND,
  DEBUG_BOUND,
  DEBUG_PACKET_DATA,
  DEBUG_ACK,
  DEBUG_HEARTBEAT,
  DEBUG_RELEASED,
};

enum debug_release_reason {
  DEBUG_RELEASE_CONTINUE,
  DEBUG_RELEASE_IDLE,
};

/* One IPv4 UDP datagram. Integer fields are big endian. Direction, boot,
 * stop generation and host-chosen session identify one pinned LAN exchange;
 * they do not authenticate it. sequence/ack order transport DATA independently
 * in each direction. command identifies an RSP operation across its retries,
 * including reads with device side effects. ACK/heartbeat carry no payload.
 * HELLO: digest[32], name length, three zero bytes, name.
 * OFFER: MAC[6], CPU count(u16), reason, terminal, name length, zero, digest,
 * name. BIND echoes digest. BOUND is empty. RELEASED has one reason byte.
 * DATA carries complete RSP records (packet/ack/interrupt), at most 1024 bytes.
 * Host ATTACHED is set only with a GDB client; RUNNING requests a new stop reply
 * for an outstanding continue after re-entry. No old-session replay on loss. */
struct debug_packet {
  enum debug_packet_kind kind;
  uint16_t flags;
  uint8_t boot[DEBUG_NONCE_BYTES], session[DEBUG_NONCE_BYTES];
  uint64_t generation, sequence, ack, command;
  const uint8_t *payload;
  size_t length;
};

static inline uint16_t debug_get16(const uint8_t *p)
{
  return (uint16_t)p[0] << 8 | p[1];
}

static inline uint64_t debug_get64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    value = value << 8 | p[i];
  }
  return value;
}

static inline void debug_put16(uint8_t *p, uint16_t value)
{
  p[0] = value >> 8;
  p[1] = value;
}

static inline void debug_put64(uint8_t *p, uint64_t value)
{
  for (unsigned i = 0; i < 8; ++i) {
    p[7 - i] = value;
    value >>= 8;
  }
}

static inline bool debug_bytes_equal(const uint8_t *a, const uint8_t *b,
                                      size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

static inline size_t debug_encode(uint8_t bytes[DEBUG_DATAGRAM_BYTES],
                                  const struct debug_packet *packet)
{
  if (packet->kind < DEBUG_HELLO || packet->kind > DEBUG_RELEASED ||
      packet->length > DEBUG_PAYLOAD_BYTES ||
      (packet->length && !packet->payload) ||
      (packet->flags & ~(DEBUG_FLAG_TARGET | DEBUG_FLAG_ATTACHED | DEBUG_FLAG_RUNNING))) {
    return 0;
  }
  const uint8_t tag[8] = "PYXISDB";
  for (unsigned i = 0; i < 8; ++i) {
    bytes[i] = tag[i];
  }
  bytes[8] = DEBUG_PROTOCOL_VERSION;
  bytes[9] = packet->kind;
  debug_put16(bytes + 10, packet->flags);
  debug_put16(bytes + 12, packet->length);
  bytes[14] = bytes[15] = 0;
  for (unsigned i = 0; i < DEBUG_NONCE_BYTES; ++i) {
    bytes[16 + i] = packet->boot[i];
    bytes[32 + i] = packet->session[i];
  }
  debug_put64(bytes + 48, packet->generation);
  debug_put64(bytes + 56, packet->sequence);
  debug_put64(bytes + 64, packet->ack);
  debug_put64(bytes + 72, packet->command);
  for (size_t i = 0; i < packet->length; ++i) {
    bytes[DEBUG_HEADER_BYTES + i] = packet->payload[i];
  }
  return DEBUG_HEADER_BYTES + packet->length;
}

static inline bool debug_decode(const uint8_t *bytes, size_t size,
                                struct debug_packet *packet)
{
  const uint8_t tag[8] = "PYXISDB";
  if (size < DEBUG_HEADER_BYTES || size > DEBUG_DATAGRAM_BYTES ||
      !debug_bytes_equal(bytes, tag, sizeof(tag)) ||
      bytes[8] != DEBUG_PROTOCOL_VERSION || bytes[14] || bytes[15] ||
      bytes[9] < DEBUG_HELLO || bytes[9] > DEBUG_RELEASED ||
      debug_get16(bytes + 12) != size - DEBUG_HEADER_BYTES ||
      (debug_get16(bytes + 10) &
       ~(DEBUG_FLAG_TARGET | DEBUG_FLAG_ATTACHED | DEBUG_FLAG_RUNNING))) {
    return false;
  }
  packet->kind = bytes[9];
  packet->flags = debug_get16(bytes + 10);
  packet->length = size - DEBUG_HEADER_BYTES;
  packet->payload = bytes + DEBUG_HEADER_BYTES;
  for (unsigned i = 0; i < DEBUG_NONCE_BYTES; ++i) {
    packet->boot[i] = bytes[16 + i];
    packet->session[i] = bytes[32 + i];
  }
  packet->generation = debug_get64(bytes + 48);
  packet->sequence = debug_get64(bytes + 56);
  packet->ack = debug_get64(bytes + 64);
  packet->command = debug_get64(bytes + 72);
  return true;
}

#endif
