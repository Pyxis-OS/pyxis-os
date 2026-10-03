#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/cpu_local.h>
#include <arch/io_apic.h>
#include <arch/keyboard.h>
#include <kernel/keyboard.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64
#define PS2_OUTPUT_FULL (1u << 0)
#define PS2_INPUT_FULL (1u << 1)
#define PS2_AUXILIARY_DATA (1u << 5)
#define PS2_TIMEOUT_ERROR (1u << 6)
#define PS2_PARITY_ERROR (1u << 7)
#define PS2_READ_CONFIG 0x20
#define PS2_WRITE_CONFIG 0x60
#define PS2_DISABLE_KEYBOARD 0xad
#define PS2_ENABLE_KEYBOARD 0xae
#define PS2_DISABLE_AUXILIARY 0xa7
#define PS2_CONFIG_KEYBOARD_IRQ (1u << 0)
#define PS2_CONFIG_AUXILIARY_IRQ (1u << 1)
#define PS2_CONFIG_KEYBOARD_DISABLED (1u << 4)
#define PS2_CONFIG_AUXILIARY_DISABLED (1u << 5)
#define PS2_CONFIG_TRANSLATION (1u << 6)
#define PS2_SET_SCAN_CODES 0xf0
#define PS2_ENABLE_SCANNING 0xf4
#define PS2_DISABLE_SCANNING 0xf5
#define PS2_ACK 0xfa
#define PS2_RESEND 0xfe
#define PS2_SCAN_SET_2 2
#define PS2_SCAN_QUERY_TIMEOUT_NS UINT64_C(20000000)
#define PS2_POLL_LIMIT 1000000
#define PS2_CLOCK_POLL_INTERVAL 1024u
#define PS2_COMMAND_ATTEMPTS 3
#define PS2_DRAIN_LIMIT 256
#define PS2_IRQ_BYTE_LIMIT 64
#define RAW_QUEUE_BYTES 256
#define SCAN_EXTENDED 0xe0
#define SCAN_PAUSE 0xe1
#define SCAN_RELEASE 0xf0
#define SCAN_OVERRUN_ZERO 0x00
#define SCAN_OVERRUN_ONES 0xff
#define SCAN_POWER_ON 0xaa

/* Raw set 2, with controller translation disabled. Fake shift bytes in the
 * Print Screen sequence have no extended-table entry and cannot alter Shift. */
static const enum key_code set2_keys[256] = {
  [0x01] = KEY_F9, [0x03] = KEY_F5, [0x04] = KEY_F3,
  [0x05] = KEY_F1, [0x06] = KEY_F2, [0x07] = KEY_F12,
  [0x09] = KEY_F10, [0x0a] = KEY_F8, [0x0b] = KEY_F6,
  [0x0c] = KEY_F4, [0x0d] = KEY_TAB, [0x0e] = KEY_GRAVE,
  [0x11] = KEY_LEFT_ALT, [0x12] = KEY_LEFT_SHIFT, [0x14] = KEY_LEFT_CONTROL,
  [0x15] = KEY_Q, [0x16] = KEY_1,
  [0x1a] = KEY_Z, [0x1b] = KEY_S, [0x1c] = KEY_A, [0x1d] = KEY_W, [0x1e] = KEY_2,
  [0x21] = KEY_C, [0x22] = KEY_X, [0x23] = KEY_D, [0x24] = KEY_E,
  [0x25] = KEY_4, [0x26] = KEY_3, [0x29] = KEY_SPACE,
  [0x2a] = KEY_V, [0x2b] = KEY_F, [0x2c] = KEY_T, [0x2d] = KEY_R, [0x2e] = KEY_5,
  [0x31] = KEY_N, [0x32] = KEY_B, [0x33] = KEY_H, [0x34] = KEY_G,
  [0x35] = KEY_Y, [0x36] = KEY_6,
  [0x3a] = KEY_M, [0x3b] = KEY_J, [0x3c] = KEY_U, [0x3d] = KEY_7, [0x3e] = KEY_8,
  [0x41] = KEY_COMMA, [0x42] = KEY_K, [0x43] = KEY_I, [0x44] = KEY_O,
  [0x45] = KEY_0, [0x46] = KEY_9, [0x49] = KEY_PERIOD,
  [0x4a] = KEY_SLASH, [0x4b] = KEY_L, [0x4c] = KEY_SEMICOLON,
  [0x4d] = KEY_P, [0x4e] = KEY_MINUS,
  [0x52] = KEY_APOSTROPHE, [0x54] = KEY_LEFT_BRACKET, [0x55] = KEY_EQUAL,
  [0x58] = KEY_CAPS_LOCK, [0x59] = KEY_RIGHT_SHIFT, [0x5a] = KEY_ENTER,
  [0x5b] = KEY_RIGHT_BRACKET, [0x5d] = KEY_BACKSLASH,
  [0x61] = KEY_NON_US_BACKSLASH, [0x66] = KEY_BACKSPACE,
  [0x69] = KEY_KP_1, [0x6b] = KEY_KP_4, [0x6c] = KEY_KP_7,
  [0x70] = KEY_KP_0, [0x71] = KEY_KP_PERIOD, [0x72] = KEY_KP_2,
  [0x73] = KEY_KP_5, [0x74] = KEY_KP_6, [0x75] = KEY_KP_8,
  [0x76] = KEY_ESCAPE, [0x77] = KEY_NUM_LOCK, [0x78] = KEY_F11,
  [0x79] = KEY_KP_PLUS, [0x7a] = KEY_KP_3, [0x7b] = KEY_KP_MINUS,
  [0x7c] = KEY_KP_MULTIPLY, [0x7d] = KEY_KP_9, [0x7e] = KEY_SCROLL_LOCK,
  [0x83] = KEY_F7, [0x84] = KEY_PRINT_SCREEN,
};

static const enum key_code extended_keys[256] = {
  [0x11] = KEY_RIGHT_ALT, [0x14] = KEY_RIGHT_CONTROL,
  [0x1f] = KEY_LEFT_SUPER, [0x27] = KEY_RIGHT_SUPER, [0x2f] = KEY_MENU,
  [0x4a] = KEY_KP_DIVIDE, [0x5a] = KEY_KP_ENTER,
  [0x69] = KEY_END, [0x6b] = KEY_LEFT, [0x6c] = KEY_HOME,
  [0x70] = KEY_INSERT, [0x71] = KEY_DELETE, [0x72] = KEY_DOWN,
  [0x74] = KEY_RIGHT, [0x75] = KEY_UP, [0x7a] = KEY_PAGE_DOWN,
  [0x7c] = KEY_PRINT_SCREEN, [0x7d] = KEY_PAGE_UP, [0x7e] = KEY_PAUSE,
};

static const uint8_t pause_sequence[] = {SCAN_PAUSE, 0x14, 0x77, SCAN_PAUSE,
                                       SCAN_RELEASE, 0x14, SCAN_RELEASE, 0x77};

static bool available;
/* IRQ producer and one task consumer, both on the BSP. Consumer accesses to
 * this queue disable interrupts; decoding state belongs solely to the task. */
static uint8_t raw_queue[RAW_QUEUE_BYTES];
static size_t raw_read, raw_write, raw_count;
static bool input_lost;
static bool held[KEY_COUNT];
static unsigned locks;
static bool extended, released, pause_release;
static size_t pause_index;

struct ps2_setup {
  const char *step;
  uint8_t status;
  uint8_t reply;
  bool reply_received;
  bool scan_query_missing;
};

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

static bool write_command(struct ps2_setup *setup, uint8_t command)
{
  if (!wait_input_empty(setup)) {
    return false;
  }
  outb(PS2_COMMAND_PORT, command);
  return true;
}

static bool write_data(struct ps2_setup *setup, uint8_t data)
{
  if (!wait_input_empty(setup)) {
    return false;
  }
  outb(PS2_DATA_PORT, data);
  return true;
}

enum ps2_reply_result {
  PS2_REPLY_RECEIVED,
  PS2_REPLY_MISSING,
  PS2_REPLY_ERROR,
};

static enum ps2_reply_result read_reply(struct ps2_setup *setup, uint8_t *reply,
    uint64_t timeout_ns)
{
  uint64_t start = timeout_ns ? arch_monotonic_ns() : 0;
  for (unsigned i = 0; timeout_ns || i < PS2_POLL_LIMIT; ++i) {
    if (!timeout_ns && !(i % PS2_CLOCK_POLL_INTERVAL)) {
      arch_clock_maintain();
    }
    uint8_t status = inb(PS2_STATUS_PORT);
    setup->status = status;
    if (status & PS2_OUTPUT_FULL) {
      uint8_t data = inb(PS2_DATA_PORT);
      if (!(status & PS2_AUXILIARY_DATA)) {
        setup->reply = data;
        setup->reply_received = true;
      }
      if (status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) {
        return PS2_REPLY_ERROR;
      }
      if (!(status & PS2_AUXILIARY_DATA)) {
        *reply = data;
        return PS2_REPLY_RECEIVED;
      }
    }
    if (timeout_ns) {
      if (status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) {
        return PS2_REPLY_ERROR;
      }
      if (!(i % PS2_CLOCK_POLL_INTERVAL)) {
        uint64_t now = arch_monotonic_ns();
        if (now == UINT64_MAX || now - start >= timeout_ns) {
          return PS2_REPLY_MISSING;
        }
      }
    }
    __asm__ volatile("pause");
  }
  return PS2_REPLY_MISSING;
}

static bool keyboard_command(struct ps2_setup *setup, uint8_t command)
{
  for (unsigned attempt = 0; attempt < PS2_COMMAND_ATTEMPTS; ++attempt) {
    if (!write_data(setup, command)) {
      return false;
    }
    /* Bytes already in flight before disable-scanning may precede its ACK. */
    for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
      uint8_t reply;
      if (read_reply(setup, &reply, 0) != PS2_REPLY_RECEIVED) {
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
    if (!(setup->status & PS2_AUXILIARY_DATA)) {
      setup->reply = data;
      setup->reply_received = true;
    }
    if (setup->status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) {
      return false;
    }
    if (!(setup->status & PS2_AUXILIARY_DATA) && data != PS2_SCAN_SET_2) {
      return false;
    }
  }
  setup->status = inb(PS2_STATUS_PORT);
  return !(setup->status & (PS2_OUTPUT_FULL | PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR));
}

static bool configure_keyboard(struct ps2_setup *setup)
{
  setup->step = "disable keyboard";
  if (!write_command(setup, PS2_DISABLE_KEYBOARD)) {
    return false;
  }
  setup->step = "disable auxiliary";
  if (!write_command(setup, PS2_DISABLE_AUXILIARY)) {
    return false;
  }
  setup->step = "drain output";
  for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
    setup->status = inb(PS2_STATUS_PORT);
    if (!(setup->status & PS2_OUTPUT_FULL)) {
      break;
    }
    inb(PS2_DATA_PORT);
  }
  setup->status = inb(PS2_STATUS_PORT);
  if (setup->status & PS2_OUTPUT_FULL) {
    return false;
  }

  uint8_t config;
  setup->step = "read config";
  if (!write_command(setup, PS2_READ_CONFIG) ||
      read_reply(setup, &config, 0) != PS2_REPLY_RECEIVED) {
    return false;
  }
  config &= ~(PS2_CONFIG_KEYBOARD_IRQ | PS2_CONFIG_AUXILIARY_IRQ |
              PS2_CONFIG_TRANSLATION);
  config |= PS2_CONFIG_AUXILIARY_DISABLED;
  setup->step = "write config (translation off)";
  if (!write_command(setup, PS2_WRITE_CONFIG) || !write_data(setup, config)) {
    return false;
  }
  setup->step = "enable keyboard";
  if (!write_command(setup, PS2_ENABLE_KEYBOARD)) {
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
  enum ps2_reply_result result = read_reply(setup, &scan_set, PS2_SCAN_QUERY_TIMEOUT_NS);
  if (result == PS2_REPLY_ERROR ||
      (result == PS2_REPLY_RECEIVED && scan_set != PS2_SCAN_SET_2)) {
    return false;
  }

  config &= ~PS2_CONFIG_KEYBOARD_DISABLED;
  config |= PS2_CONFIG_KEYBOARD_IRQ;
  setup->step = "enable keyboard IRQ";
  if (!write_command(setup, PS2_WRITE_CONFIG) || !write_data(setup, config)) {
    return false;
  }
  setup->step = "drain scan set reply";
  if (!drain_scan_query(setup)) {
    return false;
  }
  setup->scan_query_missing = result == PS2_REPLY_MISSING && setup->reply != PS2_SCAN_SET_2;
  setup->step = "enable scanning";
  return keyboard_command(setup, PS2_ENABLE_SCANNING);
}

void ps2_keyboard_init(void)
{
  struct ps2_setup setup = {0};
  if (!configure_keyboard(&setup)) {
    struct ps2_setup failed = setup;
    write_command(&setup, PS2_DISABLE_KEYBOARD);
    if (failed.reply_received) {
      klog("keyboard: PS/2 initialization failed at %s (status 0x%x, last reply 0x%x); input unavailable\n",
           failed.step, failed.status, failed.reply);
    } else {
      klog("keyboard: PS/2 initialization failed at %s (status 0x%x, no reply); input unavailable\n",
           failed.step, failed.status);
    }
    return;
  }

  available = true;
  io_apic_keyboard_enable();
  /* Capture any key that arrived between the final ACK and unmasking IRQ 1. */
  ps2_keyboard_interrupt();
  if (setup.scan_query_missing) {
    klog("keyboard: scan set read-back unavailable; using ACKed set 2\n");
  }
  klog("keyboard: PS/2 scan set 2 ready\n");
}

void ps2_keyboard_interrupt(void)
{
  for (unsigned i = 0; i < PS2_IRQ_BYTE_LIMIT; ++i) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_OUTPUT_FULL)) {
      break;
    }
    uint8_t data = inb(PS2_DATA_PORT);
    if (status & PS2_AUXILIARY_DATA) {
      continue;
    }
    if ((status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) ||
        data == SCAN_OVERRUN_ZERO || data == SCAN_OVERRUN_ONES || data == SCAN_POWER_ON ||
        raw_count == RAW_QUEUE_BYTES) {
      input_lost = true;
    }
    if (!available || input_lost) {
      continue;
    }
    raw_queue[raw_write] = data;
    raw_write = (raw_write + 1) % RAW_QUEUE_BYTES;
    ++raw_count;
  }
}

bool keyboard_available(void)
{
  return available;
}

enum raw_result {
  RAW_EMPTY,
  RAW_BYTE,
  RAW_LOST,
};

static enum raw_result read_raw(uint8_t *data)
{
  uint64_t flags = cpu_save_interrupts();
  enum raw_result result = RAW_EMPTY;
  if (input_lost) {
    raw_read = raw_write = raw_count = 0;
    input_lost = false;
    result = RAW_LOST;
  } else if (raw_count) {
    *data = raw_queue[raw_read];
    raw_read = (raw_read + 1) % RAW_QUEUE_BYTES;
    --raw_count;
    result = RAW_BYTE;
  }
  cpu_restore_interrupts(flags);
  return result;
}

static unsigned modifiers(void)
{
  unsigned state = locks;
  if (held[KEY_LEFT_SHIFT] || held[KEY_RIGHT_SHIFT]) {
    state |= KEY_MOD_SHIFT;
  }
  if (held[KEY_LEFT_CONTROL] || held[KEY_RIGHT_CONTROL]) {
    state |= KEY_MOD_CONTROL;
  }
  if (held[KEY_LEFT_ALT] || held[KEY_RIGHT_ALT]) {
    state |= KEY_MOD_ALT;
  }
  if (held[KEY_LEFT_SUPER] || held[KEY_RIGHT_SUPER]) {
    state |= KEY_MOD_SUPER;
  }
  return state;
}

static void key_event(enum key_code key, bool release, struct key_event *event)
{
  enum key_action action = release ? KEY_RELEASE : held[key] ? KEY_REPEAT : KEY_PRESS;
  held[key] = !release;
  if (action == KEY_PRESS) {
    switch (key) {
    case KEY_CAPS_LOCK: locks ^= KEY_MOD_CAPS_LOCK; break;
    case KEY_NUM_LOCK: locks ^= KEY_MOD_NUM_LOCK; break;
    case KEY_SCROLL_LOCK: locks ^= KEY_MOD_SCROLL_LOCK; break;
    default: break;
    }
  }
  *event = (struct key_event){.key = key, .action = action, .modifiers = modifiers()};
}

static void reset_state(struct key_event *event)
{
  memset(held, 0, sizeof(held));
  locks = 0;
  extended = released = pause_release = false;
  pause_index = 0;
  *event = (struct key_event){.action = KEY_STATE_RESET};
}

static bool decode(uint8_t byte, struct key_event *event)
{
  if (pause_index) {
    if (byte != pause_sequence[pause_index]) {
      reset_state(event);
      return true;
    }
    if (++pause_index == sizeof(pause_sequence)) {
      pause_index = 0;
      /* Pause has no separate wire release. Expose it as a press/release pair. */
      pause_release = true;
      key_event(KEY_PAUSE, false, event);
      return true;
    }
    return false;
  }

  if (byte == SCAN_PAUSE) {
    extended = released = false;
    pause_index = 1;
    return false;
  }
  if (byte == SCAN_EXTENDED) {
    extended = true;
    return false;
  }
  if (byte == SCAN_RELEASE) {
    released = true;
    return false;
  }

  enum key_code key = extended ? extended_keys[byte] : set2_keys[byte];
  bool release = released;
  extended = released = false;
  if (key == KEY_NONE) {
    return false;
  }
  key_event(key, release, event);
  return true;
}

bool keyboard_read_event(struct key_event *event)
{
  KASSERT(cpu_current() == cpu_bsp() && event);
  if (!available) {
    return false;
  }
  if (pause_release) {
    pause_release = false;
    key_event(KEY_PAUSE, true, event);
    return true;
  }

  for (;;) {
    uint8_t byte;
    switch (read_raw(&byte)) {
    case RAW_EMPTY:
      return false;
    case RAW_LOST:
      reset_state(event);
      return true;
    case RAW_BYTE:
      if (decode(byte, event)) {
        return true;
      }
      break;
    }
  }
}
