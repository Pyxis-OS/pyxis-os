#ifndef PXE_SHEBANG_H
#define PXE_SHEBANG_H

#include <stddef.h>

#define SHEBANG_LINE_MAX 1024
#define SHEBANG_PREFIX_SIZE (SHEBANG_LINE_MAX + 1)

enum shebang_result {
  SHEBANG_NONE,
  SHEBANG_OK,
  SHEBANG_INVALID,
  SHEBANG_TOO_LONG,
};

struct shebang {
  const char *interpreter;
  size_t length;
};

/* Inspect at most PREFIX_SIZE bytes from offset zero. A shorter prefix must
 * extend through LF or EOF. LINE_MAX counts bytes before LF, including CR in
 * CRLF. EOF may terminate the first line; a lone CR is not a line ending.
 * Accept #!, optional spaces/tabs, and one explicit scheme://file URI. No
 * arguments, quoting or trailing whitespace. The returned span borrows bytes;
 * output is cleared unless OK. This parser performs no lookup or allocation. */
enum shebang_result shebang_parse(const void *bytes, size_t size, struct shebang *result);

#endif
