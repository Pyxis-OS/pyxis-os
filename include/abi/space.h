#ifndef ABI_SPACE_H
#define ABI_SPACE_H

#include <abi/message.h>

#define SPACE_RIGHT_SET_TITLE (UINT64_C(1) << 0)
#define SPACE_SET_TITLE UINT64_C(1)
#define SPACE_TITLE_MAX 63

/* SET_TITLE requires SET_TITLE authority and the object's own space. Titles
 * contain 1..63 printable ASCII bytes (0x20..0x7e), without a terminating NUL
 * in length. No reply. Failure leaves the old title intact. Titles are labels,
 * not identities or authority, and survive the last handle and process exit.
 * The tab clips text to its fixed width; duplicate titles are allowed. */
struct space_title_request {
  struct message_header header;
  uint64_t title;
  uint64_t length;
};

_Static_assert(sizeof(struct space_title_request) == 32, "space title request layout");

#endif
