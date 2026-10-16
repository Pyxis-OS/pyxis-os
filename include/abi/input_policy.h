#ifndef ABI_INPUT_POLICY_H
#define ABI_INPUT_POLICY_H

#include <abi/message.h>

#define INPUT_POLICY_RIGHT_INSTALL_ONCE (UINT64_C(1) << 0)
#define INPUT_POLICY_INSTALL_ONCE UINT64_C(1)
#define INPUT_TOUCHPAD_SPEED_MIN (-1000)
#define INPUT_TOUCHPAD_SPEED_MAX 1000

enum input_scroll_direction {
  INPUT_SCROLL_NATURAL,
  INPUT_SCROLL_TRADITIONAL,
};

/* Stock boot init alone receives this authority. Invalid requests do not
 * consume it; the first successful installation consumes it until reboot.
 * No query or live setter exists. Pre-install policy is natural on both,
 * with default touchpad speed. Speed is signed thousandths of the normalized
 * user setting [-1, 1]; reserved0 must be zero. */
struct input_policy_request {
  struct message_header header;
  uint32_t mouse_scroll_direction, touchpad_scroll_direction;
  int32_t touchpad_speed;
  uint32_t reserved0;
};

_Static_assert(sizeof(struct input_policy_request) == 32, "input policy layout");

#endif
