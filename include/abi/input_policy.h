#ifndef ABI_INPUT_POLICY_H
#define ABI_INPUT_POLICY_H

#include <abi/message.h>

#define INPUT_POLICY_RIGHT_INSTALL_ONCE (UINT64_C(1) << 0)
#define INPUT_POLICY_INSTALL_ONCE UINT64_C(1)

enum input_scroll_direction {
  INPUT_SCROLL_NATURAL,
  INPUT_SCROLL_TRADITIONAL,
};

/* Stock boot init alone receives this authority. Invalid requests do not
 * consume it; the first successful installation consumes it until reboot.
 * No query or live setter exists. Pre-install policy is natural on both. */
struct input_policy_request {
  struct message_header header;
  uint32_t mouse_scroll_direction, touchpad_scroll_direction;
};

_Static_assert(sizeof(struct input_policy_request) == 24, "input policy layout");

#endif
