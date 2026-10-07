#ifndef REMOTE_LOG_H
#define REMOTE_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_UDP_PORT 2325u
#define LOG_UDP_VERSION 1u
#define LOG_UDP_HEADER_SIZE 52u
#define LOG_UDP_TEXT_MAX 1024u
#define LOG_UDP_DATAGRAM_MAX (LOG_UDP_HEADER_SIZE + LOG_UDP_TEXT_MAX)
#define LOG_UDP_FLAG_FATAL (1u << 0)
#define LOG_UDP_FLAG_START (1u << 1)
#define LOG_UDP_FLAGS (LOG_UDP_FLAG_FATAL | LOG_UDP_FLAG_START)

/* Datagram: tag[8], version u8, flags u8, text length BE u16, MAC[6],
 * zero[2], packet sequence BE u64, line BE u64, offset BE u64, boot stamp
 * BE u64, then 1..1024 text bytes. Packet sequences are global per boot;
 * normal and fatal text have separate line/offset cursors. START marks packet
 * zero. The sampled boot stamp and MAC are best-effort grouping, not identity
 * guarantees or authentication. This struct is never sent on the wire. */
struct log_udp_fragment {
  uint8_t flags;
  uint8_t mac[6];
  uint64_t sequence;
  uint64_t line;
  uint64_t offset;
  uint64_t boot;
  uint16_t length;
};

static inline bool log_udp_mac_valid(const unsigned char *mac)
{
  if (mac[0] & 1u) {
    return false;
  }
  unsigned char any = 0;
  for (size_t i = 0; i < 6; ++i) {
    any |= mac[i];
  }
  return any != 0;
}

static inline uint64_t log_udp_decode_u64(const unsigned char *bytes)
{
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    value = value << 8 | bytes[i];
  }
  return value;
}

static inline void log_udp_encode_u64(unsigned char *bytes, uint64_t value)
{
  for (size_t i = 0; i < 8; ++i) {
    bytes[i] = (unsigned char)(value >> (56 - 8 * i));
  }
}

/* The caller supplies a valid fragment and storage for the complete header. */
static inline void log_udp_encode_header(unsigned char *header,
    const struct log_udp_fragment *fragment)
{
  const unsigned char tag[8] = "PYXISLG";
  for (size_t i = 0; i < sizeof(tag); ++i) {
    header[i] = tag[i];
  }
  header[8] = LOG_UDP_VERSION;
  header[9] = fragment->flags;
  header[10] = (unsigned char)(fragment->length >> 8);
  header[11] = (unsigned char)fragment->length;
  for (size_t i = 0; i < 6; ++i) {
    header[12 + i] = fragment->mac[i];
  }
  header[18] = 0;
  header[19] = 0;
  log_udp_encode_u64(header + 20, fragment->sequence);
  log_udp_encode_u64(header + 28, fragment->line);
  log_udp_encode_u64(header + 36, fragment->offset);
  log_udp_encode_u64(header + 44, fragment->boot);
}

static inline bool log_udp_decode(const unsigned char *bytes, size_t size,
    struct log_udp_fragment *fragment)
{
  if (size < LOG_UDP_HEADER_SIZE || size > LOG_UDP_DATAGRAM_MAX) {
    return false;
  }
  const unsigned char tag[8] = "PYXISLG";
  for (size_t i = 0; i < sizeof(tag); ++i) {
    if (bytes[i] != tag[i]) {
      return false;
    }
  }
  uint16_t length = (uint16_t)bytes[10] << 8 | bytes[11];
  if (bytes[8] != LOG_UDP_VERSION || (bytes[9] & ~LOG_UDP_FLAGS) ||
      bytes[18] || bytes[19] || !length || length > LOG_UDP_TEXT_MAX ||
      size != LOG_UDP_HEADER_SIZE + length || !log_udp_mac_valid(bytes + 12)) {
    return false;
  }
  uint64_t sequence = log_udp_decode_u64(bytes + 20);
  if ((bytes[9] & LOG_UDP_FLAG_START) && sequence != 0) {
    return false;
  }
  fragment->flags = bytes[9];
  for (size_t i = 0; i < 6; ++i) {
    fragment->mac[i] = bytes[12 + i];
  }
  fragment->sequence = sequence;
  fragment->line = log_udp_decode_u64(bytes + 28);
  fragment->offset = log_udp_decode_u64(bytes + 36);
  fragment->boot = log_udp_decode_u64(bytes + 44);
  fragment->length = length;
  return true;
}

#endif
