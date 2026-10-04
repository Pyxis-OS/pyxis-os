#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/io_apic.h>
#include <arch/ps2.h>
#include <kernel/log.h>

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_OUTPUT_FULL (1u << 0)
#define PS2_INPUT_FULL (1u << 1)
#define PS2_AUXILIARY_DATA (1u << 5)
#define PS2_READ_CONFIG 0x20
#define PS2_WRITE_CONFIG 0x60
#define PS2_DISABLE_AUXILIARY 0xa7
#define PS2_ENABLE_AUXILIARY 0xa8
#define PS2_TEST_AUXILIARY 0xa9
#define PS2_DISABLE_KEYBOARD 0xad
#define PS2_ENABLE_KEYBOARD 0xae
#define PS2_PORT_TEST_PASSED 0x00
#define PS2_CONFIG_KEYBOARD_IRQ (1u << 0)
#define PS2_CONFIG_AUXILIARY_IRQ (1u << 1)
#define PS2_CONFIG_KEYBOARD_DISABLED (1u << 4)
#define PS2_CONFIG_AUXILIARY_DISABLED (1u << 5)
#define PS2_CONFIG_TRANSLATION (1u << 6)
#define PS2_SET_SCAN_CODES 0xf0
#define PS2_ENABLE_SCANNING 0xf4
#define PS2_DISABLE_SCANNING 0xf5
#define PS2_SCAN_SET_2 2
#define PS2_SCAN_QUERY_TIMEOUT_NS UINT64_C(20000000)
#define PS2_POLL_LIMIT 1000000
#define PS2_CLOCK_POLL_INTERVAL 1024u
#define PS2_IRQ_BYTE_LIMIT 64

static bool wait_input_empty(struct ps2_setup *setup)
{
  for (unsigned i = 0; i < PS2_POLL_LIMIT; ++i) {
    if (!(i % PS2_CLOCK_POLL_INTERVAL)) {
      arch_clock_maintain();
    }
    setup->status = inb(PS2_STATUS_PORT);
    if (!(setup->status & PS2_INPUT_FULL)) {
      return true;
    }
    __asm__ volatile("pause");
  }
  return false;
}

bool ps2_write_command(struct ps2_setup *setup, uint8_t command)
{
  if (!wait_input_empty(setup)) {
    return false;
  }
  outb(PS2_COMMAND_PORT, command);
  return true;
}

bool ps2_write_data(struct ps2_setup *setup, uint8_t data)
{
  if (!wait_input_empty(setup)) {
    return false;
  }
  outb(PS2_DATA_PORT, data);
  return true;
}

static void route_byte(uint8_t status, uint8_t data)
{
  if (status & PS2_AUXILIARY_DATA) {
    mouse_receive(status, data);
  } else {
    keyboard_receive(status, data);
  }
}

enum ps2_reply_result ps2_read_reply(struct ps2_setup *setup, enum ps2_channel channel,
                                     uint8_t *reply, uint64_t timeout_ns)
{
  uint8_t channel_status = channel == PS2_AUXILIARY_CHANNEL ? PS2_AUXILIARY_DATA : 0;
  uint64_t start = timeout_ns ? arch_monotonic_ns() : 0;
  for (unsigned i = 0; timeout_ns || i < PS2_POLL_LIMIT; ++i) {
    if (!timeout_ns && !(i % PS2_CLOCK_POLL_INTERVAL)) {
      arch_clock_maintain();
    }
    uint8_t status = inb(PS2_STATUS_PORT);
    setup->status = status;
    if (status & PS2_OUTPUT_FULL) {
      uint8_t data = inb(PS2_DATA_PORT);
      if ((status & PS2_AUXILIARY_DATA) == channel_status) {
        setup->reply = data;
        setup->reply_received = true;
        if (status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) {
          return PS2_REPLY_ERROR;
        }
        *reply = data;
        return PS2_REPLY_RECEIVED;
      }
      /* The other device's byte, including its errors, belongs to that device. */
      route_byte(status, data);
    } else if (timeout_ns && (status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR))) {
      return PS2_REPLY_ERROR;
    }
    if (timeout_ns && !(i % PS2_CLOCK_POLL_INTERVAL)) {
      uint64_t now = arch_monotonic_ns();
      if (now == UINT64_MAX || now - start >= timeout_ns) {
        return PS2_REPLY_MISSING;
      }
    }
    __asm__ volatile("pause");
  }
  return PS2_REPLY_MISSING;
}

static bool write_config(struct ps2_setup *setup, uint8_t config)
{
  return ps2_write_command(setup, PS2_WRITE_CONFIG) && ps2_write_data(setup, config);
}

static bool drain_output(struct ps2_setup *setup)
{
  for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
    setup->status = inb(PS2_STATUS_PORT);
    if (!(setup->status & PS2_OUTPUT_FULL)) {
      break;
    }
    inb(PS2_DATA_PORT);
  }
  setup->status = inb(PS2_STATUS_PORT);
  return !(setup->status & PS2_OUTPUT_FULL);
}

static bool keyboard_command(struct ps2_setup *setup, uint8_t command)
{
  for (unsigned attempt = 0; attempt < PS2_COMMAND_ATTEMPTS; ++attempt) {
    if (!ps2_write_data(setup, command)) {
      return false;
    }
    /* Bytes already in flight before disable-scanning may precede its ACK. */
    for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
      uint8_t reply;
      if (ps2_read_reply(setup, PS2_KEYBOARD_CHANNEL, &reply, 0) != PS2_REPLY_RECEIVED) {
        return false;
      }
      if (reply == PS2_ACK) {
        return true;
      }
      if (reply == PS2_RESEND) {
        break;
      }
    }
  }
  return false;
}

static bool drain_scan_query(struct ps2_setup *setup)
{
  for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
    setup->status = inb(PS2_STATUS_PORT);
    if (!(setup->status & PS2_OUTPUT_FULL)) {
      return !(setup->status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR));
    }
    uint8_t data = inb(PS2_DATA_PORT);
    if (setup->status & PS2_AUXILIARY_DATA) {
      continue;
    }
    setup->reply = data;
    setup->reply_received = true;
    if (setup->status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) {
      return false;
    }
    if (data != PS2_SCAN_SET_2) {
      return false;
    }
  }
  setup->status = inb(PS2_STATUS_PORT);
  return !(setup->status & (PS2_OUTPUT_FULL | PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR));
}

static bool read_config(struct ps2_setup *setup, uint8_t *config)
{
  return ps2_write_command(setup, PS2_READ_CONFIG) &&
         ps2_read_reply(setup, PS2_KEYBOARD_CHANNEL, config, 0) == PS2_REPLY_RECEIVED;
}

/* Runs while the keyboard port is disabled, so every controller reply is
 * unambiguous. A single-port controller would pass auxiliary commands to the
 * keyboard, so its absence must be established before any are sent. */
static bool probe_auxiliary(struct ps2_setup *setup)
{
  uint8_t config, result;
  setup->step = "enable auxiliary port";
  if (!ps2_write_command(setup, PS2_ENABLE_AUXILIARY)) {
    return false;
  }
  setup->step = "read config (auxiliary enabled)";
  if (!read_config(setup, &config) || (config & PS2_CONFIG_AUXILIARY_DISABLED)) {
    return false;
  }
  setup->step = "disable auxiliary port";
  if (!ps2_write_command(setup, PS2_DISABLE_AUXILIARY)) {
    return false;
  }
  setup->step = "read config (auxiliary disabled)";
  if (!read_config(setup, &config) || !(config & PS2_CONFIG_AUXILIARY_DISABLED)) {
    return false;
  }
  setup->step = "test auxiliary port";
  return ps2_write_command(setup, PS2_TEST_AUXILIARY) &&
         ps2_read_reply(setup, PS2_KEYBOARD_CHANNEL, &result, 0) == PS2_REPLY_RECEIVED &&
         result == PS2_PORT_TEST_PASSED;
}

static void log_mouse_failure(const struct ps2_setup *setup)
{
  if (setup->reply_received) {
    klog("mouse: PS/2 initialization failed at %s (status 0x%x, last reply 0x%x); mouse unavailable\n",
         setup->step, setup->status, setup->reply);
  } else {
    klog("mouse: PS/2 initialization failed at %s (status 0x%x, no reply); mouse unavailable\n",
         setup->step, setup->status);
  }
}

/* Best effort: a controller that rejects this is already failing the keyboard. */
static void disable_mouse(uint8_t *config)
{
  struct ps2_setup setup = {0};
  *config &= ~PS2_CONFIG_AUXILIARY_IRQ;
  *config |= PS2_CONFIG_AUXILIARY_DISABLED;
  write_config(&setup, *config);
  ps2_write_command(&setup, PS2_DISABLE_AUXILIARY);
}

/* Keyboard scanning is stopped, so only auxiliary replies are expected. */
static bool configure_mouse(uint8_t *config)
{
  struct ps2_setup setup = {.step = "enable auxiliary port"};
  uint8_t enabled = (*config & ~PS2_CONFIG_AUXILIARY_DISABLED) | PS2_CONFIG_AUXILIARY_IRQ;
  if (write_config(&setup, enabled) && mouse_configure(&setup)) {
    *config = enabled;
    return true;
  }
  log_mouse_failure(&setup);
  disable_mouse(config);
  return false;
}

static bool configure_keyboard(struct ps2_setup *setup, uint8_t *config, bool *mouse)
{
  *mouse = false;
  setup->step = "disable keyboard";
  if (!ps2_write_command(setup, PS2_DISABLE_KEYBOARD)) {
    return false;
  }
  setup->step = "disable auxiliary";
  if (!ps2_write_command(setup, PS2_DISABLE_AUXILIARY)) {
    return false;
  }
  setup->step = "drain output";
  if (!drain_output(setup)) {
    return false;
  }

  bool auxiliary = false;
  if (io_apic_mouse_available()) {
    struct ps2_setup probe = {0};
    auxiliary = probe_auxiliary(&probe);
    if (!auxiliary) {
      log_mouse_failure(&probe);
    }
    /* A failed probe can leave a late controller reply behind. */
    setup->step = "drain auxiliary probe";
    if (!drain_output(setup)) {
      return false;
    }
  }

  setup->step = "read config";
  if (!read_config(setup, config)) {
    return false;
  }
  *config &= ~(PS2_CONFIG_KEYBOARD_IRQ | PS2_CONFIG_AUXILIARY_IRQ |
               PS2_CONFIG_TRANSLATION);
  *config |= PS2_CONFIG_AUXILIARY_DISABLED;
  setup->step = "write config (translation off)";
  if (!write_config(setup, *config)) {
    return false;
  }
  setup->step = "enable keyboard";
  if (!ps2_write_command(setup, PS2_ENABLE_KEYBOARD)) {
    return false;
  }
  setup->step = "disable scanning";
  if (!keyboard_command(setup, PS2_DISABLE_SCANNING)) {
    return false;
  }
  setup->step = "set scan set command";
  if (!keyboard_command(setup, PS2_SET_SCAN_CODES)) {
    return false;
  }
  setup->step = "set scan set 2";
  if (!keyboard_command(setup, PS2_SCAN_SET_2)) {
    return false;
  }

  /* An ACKed set-2 selection is sufficient when firmware omits the query ID.
   * Keep scanning stopped while waiting and draining delayed query output. */
  uint8_t scan_set;
  setup->step = "query scan set command";
  if (!keyboard_command(setup, PS2_SET_SCAN_CODES)) {
    return false;
  }
  setup->step = "query scan set value";
  if (!keyboard_command(setup, 0)) {
    return false;
  }
  setup->step = "read scan set";
  enum ps2_reply_result result = ps2_read_reply(setup, PS2_KEYBOARD_CHANNEL, &scan_set,
                                                PS2_SCAN_QUERY_TIMEOUT_NS);
  if (result == PS2_REPLY_ERROR ||
      (result == PS2_REPLY_RECEIVED && scan_set != PS2_SCAN_SET_2)) {
    return false;
  }

  *config &= ~PS2_CONFIG_KEYBOARD_DISABLED;
  *config |= PS2_CONFIG_KEYBOARD_IRQ;
  setup->step = "enable keyboard IRQ";
  if (!write_config(setup, *config)) {
    return false;
  }
  setup->step = "drain scan set reply";
  if (!drain_scan_query(setup)) {
    return false;
  }
  setup->scan_query_missing = result == PS2_REPLY_MISSING && setup->reply != PS2_SCAN_SET_2;

  if (auxiliary) {
    *mouse = configure_mouse(config);
  }
  setup->step = "enable scanning";
  return keyboard_command(setup, PS2_ENABLE_SCANNING);
}

void ps2_init(void)
{
  struct ps2_setup setup = {0};
  uint8_t config = 0;
  bool mouse;
  if (!configure_keyboard(&setup, &config, &mouse)) {
    struct ps2_setup failed = setup;
    ps2_write_command(&setup, PS2_DISABLE_KEYBOARD);
    ps2_write_command(&setup, PS2_DISABLE_AUXILIARY);
    if (failed.reply_received) {
      klog("keyboard: PS/2 initialization failed at %s (status 0x%x, last reply 0x%x); input unavailable\n",
           failed.step, failed.status, failed.reply);
    } else {
      klog("keyboard: PS/2 initialization failed at %s (status 0x%x, no reply); input unavailable\n",
           failed.step, failed.status);
    }
    return;
  }

  keyboard_start();
  io_apic_keyboard_enable();
  if (setup.scan_query_missing) {
    klog("keyboard: scan set read-back unavailable; using ACKed set 2\n");
  }
  klog("keyboard: PS/2 scan set 2 ready\n");

  /* Keyboard bytes arriving while this waits for its ACK enter the started
   * keyboard queue. Mouse packets before the route is unmasked stay buffered. */
  if (mouse) {
    struct ps2_setup reporting = {0};
    if (mouse_enable_reporting(&reporting)) {
      io_apic_mouse_enable();
    } else {
      log_mouse_failure(&reporting);
      disable_mouse(&config);
    }
  }
  /* Capture any byte that arrived between the final ACK and unmasking. */
  ps2_interrupt();
}

void ps2_interrupt(void)
{
  for (unsigned i = 0; i < PS2_IRQ_BYTE_LIMIT; ++i) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_OUTPUT_FULL)) {
      break;
    }
    route_byte(status, inb(PS2_DATA_PORT));
  }
}
