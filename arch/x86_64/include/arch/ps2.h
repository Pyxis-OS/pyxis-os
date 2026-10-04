#ifndef ARCH_PS2_H
#define ARCH_PS2_H

#include <stdint.h>

/* BSP, IF=0; the PS/2 I/O APIC routes must already be initialized/masked.
 * Configures the keyboard, then the optional mouse. A mouse failure disables
 * only the auxiliary port and never fails keyboard setup. */
void ps2_init(void);
/* Entry for both PS/2 vectors. Routes each byte to the keyboard or mouse by
 * the controller status register. Caller acknowledges the local APIC afterward. */
void ps2_interrupt(void);

/* Controller access shared by the keyboard and mouse drivers. Setup runs on
 * the BSP with IF=0 and records its last step, status and reply for diagnostics. */
#define PS2_TIMEOUT_ERROR (1u << 6)
#define PS2_PARITY_ERROR (1u << 7)
#define PS2_ACK 0xfa
#define PS2_RESEND 0xfe
#define PS2_COMMAND_ATTEMPTS 3
#define PS2_DRAIN_LIMIT 256

struct ps2_setup {
  const char *step;
  uint8_t status;
  uint8_t reply;
  bool reply_received;
  bool scan_query_missing;
};

enum ps2_channel {
  PS2_KEYBOARD_CHANNEL,
  PS2_AUXILIARY_CHANNEL,
};

enum ps2_reply_result {
  PS2_REPLY_RECEIVED,
  PS2_REPLY_MISSING,
  PS2_REPLY_ERROR,
};

bool ps2_write_command(struct ps2_setup *setup, uint8_t command);
bool ps2_write_data(struct ps2_setup *setup, uint8_t data);
/* Waits for the next byte from channel. A zero timeout uses a bounded poll
 * count. Bytes from the other channel go to that device's receive path. */
enum ps2_reply_result ps2_read_reply(struct ps2_setup *setup, enum ps2_channel channel,
                                     uint8_t *reply, uint64_t timeout_ns);

/* Device receive paths: IRQ or setup context, IF=0. Each accepts bytes only
 * after its device is started and latches loss on errors or a full queue. */
void keyboard_receive(uint8_t status, uint8_t data);
void keyboard_start(void);
void mouse_receive(uint8_t status, uint8_t data);
/* Mouse device setup through the auxiliary port: reset, defaults, wheel probe.
 * Leaves reporting disabled; mouse_enable_reporting() finishes setup. */
bool mouse_configure(struct ps2_setup *setup);
bool mouse_enable_reporting(struct ps2_setup *setup);

#endif
