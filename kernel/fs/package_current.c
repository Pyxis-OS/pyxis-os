#include <kernel/string.h>
#include <kernel/memory.h>
#include "package_current.h"

struct current_parser {
  const char *bytes;
  size_t size, position;
};

static char peek(const struct current_parser *parser)
{
  return parser->position < parser->size ? parser->bytes[parser->position] : 0;
}

static bool space(char byte)
{
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' ||
      byte == '\v' || byte == '\f';
}

static bool digit(char byte)
{
  return byte >= '0' && byte <= '9';
}

static bool identifier(char byte)
{
  return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
      byte == '_' || digit(byte);
}

static int hex(char byte)
{
  if (digit(byte)) return byte - '0';
  if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
  return -1;
}

static size_t bracket(const struct current_parser *parser, size_t start, char delimiter)
{
  if (start >= parser->size || parser->bytes[start] != delimiter) return start;
  size_t end = start + 1;
  while (end < parser->size && parser->bytes[end] == '=') ++end;
  return end < parser->size && parser->bytes[end] == delimiter ? end + 1 : start;
}

static bool long_body(struct current_parser *parser, size_t *start, size_t *end)
{
  size_t opening = parser->position;
  size_t body = bracket(parser, opening, '[');
  if (body == opening) return false;
  parser->position = body;
  while (parser->position < parser->size) {
    size_t close = bracket(parser, parser->position, ']');
    if (close - parser->position == body - opening) {
      *start = body;
      *end = parser->position;
      parser->position = close;
      return true;
    }
    ++parser->position;
  }
  return false;
}

static bool skip(struct current_parser *parser)
{
  for (;;) {
    if (space(peek(parser))) {
      ++parser->position;
    } else if (peek(parser) == '-' && parser->position + 1 < parser->size &&
        parser->bytes[parser->position + 1] == '-') {
      parser->position += 2;
      if (bracket(parser, parser->position, '[') != parser->position) {
        size_t start, end;
        if (!long_body(parser, &start, &end)) return false;
      } else {
        while (peek(parser) && peek(parser) != '\r' && peek(parser) != '\n') {
          ++parser->position;
        }
      }
    } else {
      return true;
    }
  }
}

static void newline(struct current_parser *parser)
{
  char first = peek(parser);
  ++parser->position;
  if ((first == '\r' && peek(parser) == '\n') ||
      (first == '\n' && peek(parser) == '\r')) ++parser->position;
}

static bool quoted_key(struct current_parser *parser, char key[7])
{
  char quote = peek(parser);
  ++parser->position;
  size_t used = 0;
  while (parser->position < parser->size) {
    char byte = peek(parser);
    ++parser->position;
    if (byte == quote) {
      key[used] = 0;
      return true;
    }
    if (byte == '\r' || byte == '\n') return false;
    if (byte == '\\') {
      byte = peek(parser);
      if (!byte) return false;
      ++parser->position;
      switch (byte) {
      case 'z':
        while (space(peek(parser))) ++parser->position;
        continue;
      case '\r': case '\n':
        --parser->position;
        newline(parser);
        byte = '\n';
        break;
      case '\\': case '\'': case '"': break;
      case 'a': byte = '\a'; break;
      case 'b': byte = '\b'; break;
      case 'f': byte = '\f'; break;
      case 'n': byte = '\n'; break;
      case 'r': byte = '\r'; break;
      case 't': byte = '\t'; break;
      case 'v': byte = '\v'; break;
      case 'x': {
        int high = hex(peek(parser));
        if (high < 0) return false;
        ++parser->position;
        int low = hex(peek(parser));
        if (low < 0) return false;
        ++parser->position;
        byte = (char)(high * 16 + low);
        break;
      }
      case 'u': {
        if (peek(parser) != '{') return false;
        ++parser->position;
        unsigned number = 0;
        size_t digits = 0;
        while (hex(peek(parser)) >= 0) {
          if (number > (0x7fffffffU - (unsigned)hex(peek(parser))) / 16) return false;
          number = number * 16 + (unsigned)hex(peek(parser));
          ++parser->position;
          ++digits;
        }
        if (!digits || peek(parser) != '}' || number >= 128) return false;
        ++parser->position;
        byte = (char)number;
        break;
      }
      default: {
        if (!digit(byte)) return false;
        unsigned number = (unsigned)(byte - '0');
        for (unsigned i = 1; i < 3 && digit(peek(parser)); ++i) {
          number = number * 10 + (unsigned)(peek(parser) - '0');
          ++parser->position;
        }
        if (number > 255) return false;
        byte = (char)number;
        break;
      }
      }
    }
    /* Only the two ASCII schema keys can ever be accepted. */
    if (!byte || used == 6) return false;
    key[used++] = byte;
  }
  return false;
}

static bool string_key(struct current_parser *parser, char key[7])
{
  if (peek(parser) == '\'' || peek(parser) == '"') return quoted_key(parser, key);
  size_t start, end;
  if (!long_body(parser, &start, &end)) return false;
  if (start < end && (parser->bytes[start] == '\r' || parser->bytes[start] == '\n')) {
    char first = parser->bytes[start++];
    if (start < end && ((first == '\r' && parser->bytes[start] == '\n') ||
        (first == '\n' && parser->bytes[start] == '\r'))) ++start;
  }
  if (end - start > 6) return false;
  memcpy(key, parser->bytes + start, end - start);
  key[end - start] = 0;
  return true;
}

static bool field_key(struct current_parser *parser, char key[7])
{
  if (peek(parser) == '[' && bracket(parser, parser->position, '[') == parser->position) {
    ++parser->position;
    if (!skip(parser) || !string_key(parser, key) || !skip(parser) || peek(parser) != ']') {
      return false;
    }
    ++parser->position;
  } else {
    size_t start = parser->position;
    while (identifier(peek(parser))) ++parser->position;
    size_t length = parser->position - start;
    if (!length || length > 6) return false;
    memcpy(key, parser->bytes + start, length);
    key[length] = 0;
  }
  if (!skip(parser) || peek(parser) != '=') return false;
  ++parser->position;
  return skip(parser);
}

static bool positive_integer(struct current_parser *parser, uint64_t *value)
{
  size_t start = parser->position;
  bool negative = peek(parser) == '-';
  if (negative) ++parser->position;
  bool hexadecimal = peek(parser) == '0' && parser->position + 1 < parser->size &&
      (parser->bytes[parser->position + 1] == 'x' || parser->bytes[parser->position + 1] == 'X');
  if (hexadecimal) parser->position += 2;
  size_t digits = 0;
  uint64_t number = 0, base = hexadecimal ? 16 : 10;
  while (hexadecimal ? hex(peek(parser)) >= 0 : digit(peek(parser))) {
    unsigned next = hexadecimal ? (unsigned)hex(peek(parser)) : (unsigned)(peek(parser) - '0');
    if (number > (UINT64_MAX - next) / base) return false;
    number = number * base + next;
    ++parser->position;
    ++digits;
  }
  if (!digits || parser->position - start > 128 || identifier(peek(parser)) ||
      peek(parser) == '.') return false;
  /* The literal parser preserves hex uint64 bit patterns as signed integers;
   * decimal overflow becomes NUMBER and is refused by the current schema. */
  if (!hexadecimal && (negative || number > INT64_MAX)) return false;
  if (negative) number = 0 - number;
  if (!number || number > INT64_MAX) return false;
  *value = number;
  return true;
}

bool package_current_parse(const char *bytes, size_t size, uint64_t *set)
{
  if (!size || size > 256) return false;
  for (size_t i = 0; i < size; ++i) if (!bytes[i]) return false;
  struct current_parser parser = {.bytes = bytes, .size = size};
  if (!skip(&parser) || size - parser.position < 6 ||
      memcmp(bytes + parser.position, "return", 6)) return false;
  parser.position += 6;
  if (identifier(peek(&parser)) || !skip(&parser) || peek(&parser) != '{') return false;
  ++parser.position;
  bool seen = false;
  if (!skip(&parser)) return false;
  while (peek(&parser) != '}') {
    char key[7];
    uint64_t value;
    if (!field_key(&parser, key) || !positive_integer(&parser, &value)) return false;
    if (strlen(key) != 3 || memcmp(key, "set", 3) || seen) return false;
    seen = true;
    *set = value;
    if (!skip(&parser)) return false;
    if (peek(&parser) == ',' || peek(&parser) == ';') {
      ++parser.position;
      if (!skip(&parser)) return false;
    } else if (peek(&parser) != '}') {
      return false;
    }
  }
  ++parser.position;
  if (!seen || !skip(&parser)) return false;
  if (peek(&parser) == ';') {
    ++parser.position;
    if (!skip(&parser)) return false;
  }
  return parser.position == size;
}
