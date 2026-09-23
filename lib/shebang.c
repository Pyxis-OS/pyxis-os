#include <pxe/shebang.h>

static bool explicit_uri(const char *text, size_t length)
{
  size_t scheme = 0;
  while (scheme < length && text[scheme] != ':' && text[scheme] != '/') {
    ++scheme;
  }
  return scheme && length - scheme > 3 && text[scheme] == ':' &&
         text[scheme + 1] == '/' && text[scheme + 2] == '/';
}

enum shebang_result shebang_parse(const void *bytes, size_t size, struct shebang *result)
{
  *result = (struct shebang){0};
  const char *text = bytes;
  if (size < 2 || text[0] != '#' || text[1] != '!') {
    return SHEBANG_NONE;
  }

  size_t end = 2;
  while (end < size && end <= SHEBANG_LINE_MAX && text[end] != '\n') {
    ++end;
  }
  if (end > SHEBANG_LINE_MAX) {
    return SHEBANG_TOO_LONG;
  }
  if (end < size && text[end] == '\n' && text[end - 1] == '\r') {
    --end;
  }
  size_t start = 2;
  while (start < end && (text[start] == ' ' || text[start] == '\t')) {
    ++start;
  }
  for (size_t i = start; i < end; ++i) {
    unsigned char byte = text[i];
    if (byte <= ' ' || byte == 0x7f) {
      return SHEBANG_INVALID;
    }
  }
  if (!explicit_uri(text + start, end - start)) {
    return SHEBANG_INVALID;
  }
  *result = (struct shebang){text + start, end - start};
  return SHEBANG_OK;
}
