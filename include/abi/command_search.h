#ifndef ABI_COMMAND_SEARCH_H
#define ABI_COMMAND_SEARCH_H

#include <abi/handle.h>
#include <abi/message.h>

#define COMMAND_LIST_LABEL_MAX 63
#define COMMAND_LIST_RIGHT_READ UINT64_C(1)
#define COMMAND_LIST_INFO UINT64_C(1)
#define COMMAND_LIST_GET UINT64_C(2)

/* An immutable ordered list of native DIRECTORY grants. Labels are diagnostic
 * bytes, NUL terminated with zero padding; they never name authority to reopen.
 * Member masks are exact subsets of LOOKUP, ENUMERATE and READ_FILES, with
 * zero transport. Forwarded native directories keep their original lineage.
 * Empty lists are valid. Admission limits refuse the complete constructor. */
struct command_list_create_entry {
  char label[COMMAND_LIST_LABEL_MAX + 1];
  handle_t handle;
  uint64_t rights;
  uint64_t transport;
};

struct command_list_create_reply {
  handle_t handle; /* Owned READ list; publication uses ordinary ENVIRONMENT_SET. */
};

struct command_list_info_reply {
  uint64_t count;
  uint64_t capacity; /* Current constructor admission policy, not a format limit. */
};

struct command_list_get_message {
  struct message_header header;
  uint64_t index;
};

struct command_list_entry_reply {
  char label[COMMAND_LIST_LABEL_MAX + 1];
  handle_t handle; /* Owned local directory grant; out-of-range GET is NOT_FOUND. */
  uint64_t rights;
  uint64_t transport;
};

_Static_assert(sizeof(struct command_list_create_entry) == 88, "command list input layout");
_Static_assert(sizeof(struct command_list_entry_reply) == 88, "command list reply layout");
_Static_assert(sizeof(struct command_list_info_reply) == 16, "command list info layout");
_Static_assert(sizeof(struct command_list_get_message) == 24, "command list get layout");

#endif
