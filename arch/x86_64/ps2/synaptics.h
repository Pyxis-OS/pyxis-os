#ifndef ARCH_PS2_SYNAPTICS_H
#define ARCH_PS2_SYNAPTICS_H

#include <arch/ps2.h>
#include <kernel/mouse.h>

#define MOUSE_PS2_REPLY_TIMEOUT_NS UINT64_C(500000000)

/* Setup only, BSP with IF=0. The shared transport retains keyboard routing. */
bool mouse_ps2_command(struct ps2_setup *setup, uint8_t command);
bool mouse_ps2_reply(struct ps2_setup *setup, uint8_t *reply, uint64_t timeout_ns);
bool mouse_ps2_reset(struct ps2_setup *setup);

enum synaptics_setup_result {
  SYNAPTICS_NOT_DEVICE,
  SYNAPTICS_RELATIVE,
  SYNAPTICS_ABSOLUTE,
  SYNAPTICS_UNAVAILABLE,
};

enum synaptics_decode_result {
  SYNAPTICS_INCOMPLETE,
  SYNAPTICS_EVENT,
  SYNAPTICS_STOPPED,
};

enum synaptics_setup_result synaptics_configure(struct ps2_setup *setup, bool activate,
                                               bool diagnose);
const struct mouse_profile *synaptics_profile(void);
void synaptics_reset_decoder(void);
enum synaptics_decode_result synaptics_decode(uint8_t byte, struct mouse_event *event);

#endif
