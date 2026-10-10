#ifndef GDB_BRIDGE_RSP_H
#define GDB_BRIDGE_RSP_H

#include <debug/protocol.h>

enum rsp_result {
  RSP_MORE,
  RSP_RECORD,
  RSP_INVALID,
  RSP_OVERFLOW,
};

struct rsp_parser {
  uint8_t bytes[DEBUG_PAYLOAD_BYTES];
  size_t length;
  unsigned checksum_bytes;
  bool escaped;
};

enum rsp_result rsp_feed(struct rsp_parser *parser, uint8_t byte);
bool rsp_valid(const uint8_t *bytes, size_t length);
bool rsp_console(const uint8_t *bytes, size_t length);

#endif
