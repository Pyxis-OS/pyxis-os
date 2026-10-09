#ifndef ABI_CLIPBOARD_H
#define ABI_CLIPBOARD_H

#include <abi/message.h>
#include <abi/handle.h>

#define CLIPBOARD_RIGHT_PUBLISH (UINT64_C(1) << 0)
#define CLIPBOARD_RIGHT_PASTE (UINT64_C(1) << 1)
#define CLIPBOARD_RIGHTS (CLIPBOARD_RIGHT_PUBLISH | CLIPBOARD_RIGHT_PASTE)

#define CLIPBOARD_PUBLISH UINT64_C(1)
#define CLIPBOARD_PASTE UINT64_C(2)
#define CLIPBOARD_CLEAR UINT64_C(3)
#define CLIPBOARD_REFUSE UINT64_C(4)
#define CLIPBOARD_GRAPHICS_PUBLISH UINT64_C(5)
#define CLIPBOARD_GRAPHICS_READ UINT64_C(6)
#define CLIPBOARD_GRAPHICS_HAS UINT64_C(7)
#define CLIPBOARD_GRAPHICS_REFUSE UINT64_C(8)

#define CLIPBOARD_LAYER_LOCAL UINT64_C(1)
#define CLIPBOARD_LAYER_SHARED UINT64_C(2)

#define CLIPBOARD_TEXT_MAX UINT64_C(65536)
#define CLIPBOARD_STORAGE_MAX UINT64_C(8388608)
#define CLIPBOARD_TIMEOUT_MS UINT32_C(5000)

/* The grant selects the layer. Requests consume one matching fresh native
 * action even on refusal. No general store-read or snapshot handles exist. */
struct clipboard_publish_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t generation, mapping_identity;
  uint64_t address, length;
};

struct clipboard_paste_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t generation, mapping_identity;
  handle_t attachment;
};

struct clipboard_clear_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t generation, mapping_identity;
};

struct clipboard_refuse_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t generation, mapping_identity;
  uint64_t operation;
};

/* Graphics text is scalar UTF-8, length-delimited with no embedded NUL, and
 * preserves bytes including line endings. PUBLISH spends Copy; READ spends
 * Paste, including on matched refusal. The grant selects the layer.
 * Graphics requests use only the opaque physical action identity. READ returns
 * raw UTF-8 bytes in the reply buffer, with reply_size as its length. Supply
 * CLIPBOARD_TEXT_MAX capacity; an authorized missing/empty item returns zero.
 * HAS reports one boolean without consuming or extending a Paste action. */
struct clipboard_graphics_publish_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t address, length;
};

struct clipboard_graphics_request {
  struct message_header header;
  uint64_t action_id;
};

struct clipboard_graphics_refuse_request {
  struct message_header header;
  uint64_t action_id;
  uint64_t operation; /* CLIPBOARD_PUBLISH or CLIPBOARD_PASTE. */
};

struct clipboard_graphics_has_reply {
  uint64_t has_text;
};

_Static_assert(sizeof(struct clipboard_graphics_publish_request) == 40, "graphics publish layout");
_Static_assert(sizeof(struct clipboard_graphics_request) == 24, "graphics request layout");
_Static_assert(sizeof(struct clipboard_graphics_refuse_request) == 32, "graphics refuse layout");
_Static_assert(sizeof(struct clipboard_graphics_has_reply) == 8, "graphics has layout");

struct clipboard_paste_reply {
  uint64_t transaction_id;
};
_Static_assert(sizeof(struct clipboard_publish_request) == 56, "clipboard publish layout");
_Static_assert(sizeof(struct clipboard_paste_request) == 48, "clipboard paste layout");
_Static_assert(sizeof(struct clipboard_clear_request) == 40, "clipboard clear layout");
_Static_assert(sizeof(struct clipboard_refuse_request) == 48, "clipboard refuse layout");
_Static_assert(sizeof(struct clipboard_paste_reply) == 8, "clipboard paste reply layout");

#endif
