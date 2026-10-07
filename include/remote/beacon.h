#ifndef REMOTE_BEACON_H
#define REMOTE_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REMOTE_BEACON_PORT 2324u
#define REMOTE_BEACON_VERSION 1u
#define REMOTE_BEACON_NAME_MAX 63u
#define REMOTE_BEACON_HEADER_SIZE 16u
#define REMOTE_BEACON_MAX (REMOTE_BEACON_HEADER_SIZE + REMOTE_BEACON_NAME_MAX)

/* One UDP datagram: "PYXISRT\0", version (u8), name length (u8), TCP port
 * (big-endian u16), four zero reserved bytes, then the name without a NUL.
 * The UDP sender supplies the TCP address. Names are case-sensitive, printable
 * ASCII without spaces, fitting the unquoted kernel command-line syntax.
 * Neither the tag nor the name authenticates a peer. */
static inline size_t remote_beacon_name_length(const char *name)
{
  if (!name) {
    return 0;
  }
  for (size_t i = 0; i <= REMOTE_BEACON_NAME_MAX; ++i) {
    unsigned char c = (unsigned char)name[i];
    if (!c) {
      return i;
    }
    if (i == REMOTE_BEACON_NAME_MAX || c < 33 || c > 126) {
      return 0;
    }
  }
  return 0;
}

static inline size_t remote_beacon_encode(unsigned char bytes[REMOTE_BEACON_MAX],
    const char *name, uint16_t port)
{
  size_t length = remote_beacon_name_length(name);
  if (!length || !port) {
    return 0;
  }
  const unsigned char tag[8] = "PYXISRT";
  for (size_t i = 0; i < sizeof(tag); ++i) {
    bytes[i] = tag[i];
  }
  bytes[8] = REMOTE_BEACON_VERSION;
  bytes[9] = (unsigned char)length;
  bytes[10] = (unsigned char)(port >> 8);
  bytes[11] = (unsigned char)port;
  for (size_t i = 12; i < REMOTE_BEACON_HEADER_SIZE; ++i) {
    bytes[i] = 0;
  }
  for (size_t i = 0; i < length; ++i) {
    bytes[REMOTE_BEACON_HEADER_SIZE + i] = (unsigned char)name[i];
  }
  return REMOTE_BEACON_HEADER_SIZE + length;
}

static inline bool remote_beacon_decode(const unsigned char *bytes, size_t size,
    const char *name, uint16_t *port)
{
  size_t length = remote_beacon_name_length(name);
  if (!length || size != REMOTE_BEACON_HEADER_SIZE + length ||
      bytes[8] != REMOTE_BEACON_VERSION || bytes[9] != length) {
    return false;
  }
  const unsigned char tag[8] = "PYXISRT";
  for (size_t i = 0; i < sizeof(tag); ++i) {
    if (bytes[i] != tag[i]) {
      return false;
    }
  }
  for (size_t i = 12; i < REMOTE_BEACON_HEADER_SIZE; ++i) {
    if (bytes[i]) {
      return false;
    }
  }
  for (size_t i = 0; i < length; ++i) {
    if (bytes[REMOTE_BEACON_HEADER_SIZE + i] != (unsigned char)name[i]) {
      return false;
    }
  }
  uint16_t value = (uint16_t)bytes[10] << 8 | bytes[11];
  if (!value) {
    return false;
  }
  *port = value;
  return true;
}

#endif
