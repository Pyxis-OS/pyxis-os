#include "rsp.h"

static int hex_digit(uint8_t byte)
{
  if (byte >= '0' && byte <= '9') {
    return byte - '0';
  }
  if (byte >= 'a' && byte <= 'f') {
    return byte - 'a' + 10;
  }
  if (byte >= 'A' && byte <= 'F') {
    return byte - 'A' + 10;
  }
  return -1;
}

bool rsp_valid(const uint8_t *bytes, size_t length)
{
  if (!length || length > DEBUG_PAYLOAD_BYTES) {
    return false;
  }
  if (length == 1) {
    return bytes[0] == '+' || bytes[0] == '-' || bytes[0] == 3;
  }
  /* The target may coalesce the request acknowledgement and its reply into
   * one transport record. TCP parsing still emits each record separately. */
  if (length > 1 && (bytes[0] == '+' || bytes[0] == '-')) {
    ++bytes;
    --length;
  }
  if (length < 4 || length > DEBUG_PAYLOAD_BYTES || bytes[0] != '$' ||
      bytes[length - 3] != '#') {
    return false;
  }
  uint8_t checksum = 0;
  bool escaped = false;
  for (size_t i = 1; i < length - 3; ++i) {
    uint8_t byte = bytes[i];
    if (!escaped && (byte == '$' || byte == '#')) {
      return false;
    }
    checksum += byte;
    if (escaped) {
      escaped = false;
    } else if (byte == '}') {
      escaped = true;
    }
  }
  int high = hex_digit(bytes[length - 2]);
  int low = hex_digit(bytes[length - 1]);
  return !escaped && high >= 0 && low >= 0 && checksum == high * 16 + low;
}

enum rsp_result rsp_feed(struct rsp_parser *parser, uint8_t byte)
{
  if (!parser->length && byte != '$') {
    parser->bytes[0] = byte;
    parser->length = 1;
    return rsp_valid(parser->bytes, 1) ? RSP_RECORD : RSP_INVALID;
  }
  if (parser->length == sizeof(parser->bytes)) {
    return RSP_OVERFLOW;
  }
  parser->bytes[parser->length++] = byte;
  if (parser->checksum_bytes) {
    if (++parser->checksum_bytes == 3) {
      return rsp_valid(parser->bytes, parser->length) ? RSP_RECORD : RSP_INVALID;
    }
  } else if (parser->length > 1) {
    if (parser->escaped) {
      parser->escaped = false;
    } else if (byte == '}') {
      parser->escaped = true;
    } else if (byte == '#') {
      parser->checksum_bytes = 1;
    } else if (byte == '$') {
      return RSP_INVALID;
    }
  }
  return RSP_MORE;
}

bool rsp_console(const uint8_t *bytes, size_t length)
{
  if (length < 5 || bytes[0] != '$' || bytes[1] != 'O' ||
      ((length - 5) & 1)) {
    return false;
  }
  for (size_t i = 2; i < length - 3; ++i) {
    if (hex_digit(bytes[i]) < 0) {
      return false;
    }
  }
  return true;
}
