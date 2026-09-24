#ifndef NET_WIRE_H
#define NET_WIRE_H

#include <stddef.h>
#include <stdint.h>

/* Byte accesses avoid alignment assumptions and C bitfield wire layouts. */
static inline uint16_t net_read_u16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] << 8 | bytes[1];
}

static inline uint32_t net_read_u32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
      (uint32_t)bytes[2] << 8 | bytes[3];
}

static inline void net_write_u16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = value >> 8;
  bytes[1] = value;
}

static inline void net_write_u32(uint8_t *bytes, uint32_t value)
{
  bytes[0] = value >> 24;
  bytes[1] = value >> 16;
  bytes[2] = value >> 8;
  bytes[3] = value;
}

/* Internet checksum for a bounded IP packet/header. The odd final byte is
 * the high half of a word padded with zero; carry folds use one's complement. */
static inline uint16_t net_checksum(const uint8_t *bytes, size_t length)
{
  uint32_t sum = 0;
  while (length >= 2) {
    sum += net_read_u16(bytes);
    bytes += 2;
    length -= 2;
  }
  if (length) {
    sum += (uint16_t)bytes[0] << 8;
  }
  while (sum >> 16) {
    sum = (sum & UINT16_MAX) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

#endif
