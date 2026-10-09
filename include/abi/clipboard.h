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
#define CLIPBOARD_LAYER_LOCAL UINT64_C(1)
#define CLIPBOARD_LAYER_SHARED UINT64_C(2)
#define CLIPBOARD_TEXT_MAX UINT64_C(65536)
#define CLIPBOARD_STORAGE_MAX UINT64_C(8388608)
#define CLIPBOARD_TIMEOUT_MS UINT32_C(5000)
/* The grant selects the layer. Requests consume one matching fresh native
 * action even on refusal. No general store-read or snapshot handles exist. */
struct clipboard_publish_request {
  struct message_header header;
  uint64_t action_id, generation, mapping_identity, address, length;
};
struct clipboard_paste_request {
  struct message_header header;
  uint64_t action_id, generation, mapping_identity;
  handle_t attachment;
};
struct clipboard_clear_request {
  struct message_header header;
  uint64_t action_id, generation, mapping_identity;
};
struct clipboard_refuse_request {
  struct message_header header;
  uint64_t action_id, generation, mapping_identity, operation;
};
struct clipboard_paste_reply { uint64_t transaction_id; };
#endif
