#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/ps2.h>
#include <kernel/keyboard.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define RAW_QUEUE_BYTES 256
#define SCAN_EXTENDED 0xe0
#define SCAN_PAUSE 0xe1
#define SCAN_RELEASE 0xf0
/* Key detection error: the keyboard could not resolve that many simultaneous
 * keys, or its own buffer overran. Set 2 sends 0x00; some keyboards send 0xFF.
 * The unresolved keys are not reported; no byte in transit was lost. */
#define SCAN_KEY_ERROR_ZERO 0x00
#define SCAN_KEY_ERROR_ONES 0xff
#define SCAN_POWER_ON 0xaa
#define KEY_ERROR_LOG_INTERVAL_NS UINT64_C(1000000000)

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
static size_t key_errors; /* IRQ producer; the consumer reads it with IF=0. */
static size_t key_errors_logged;
static uint64_t key_errors_logged_at;
static bool extended, released, pause_release;
static size_t pause_index;

void keyboard_start(void)
{
  available = true;
}

void keyboard_receive(uint8_t status, uint8_t data)
{
  if (!available) {
    return;
  }
  if ((status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) || data == SCAN_POWER_ON ||
      raw_count == RAW_QUEUE_BYTES) {
    input_lost = true;
  }
  if (input_lost) {
    return;
  }
  if (data == SCAN_KEY_ERROR_ZERO || data == SCAN_KEY_ERROR_ONES) {
    ++key_errors;
    return;
  }
  raw_queue[raw_write] = data;
  raw_write = (raw_write + 1) % RAW_QUEUE_BYTES;
  ++raw_count;
}

bool ps2_keyboard_available(void)
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

static void key_event(enum key_code key, bool release, struct key_event *event)
{
  *event = (struct key_event){.key = key, .action = release ? KEY_RELEASE : KEY_PRESS};
}

static void reset_state(struct key_event *event)
{
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

/* Task context only: logging from the IRQ could contend for the log lock. */
static void report_key_errors(void)
{
  uint64_t flags = cpu_save_interrupts();
  size_t total = key_errors;
  cpu_restore_interrupts(flags);
  if (total == key_errors_logged) {
    return;
  }
  uint64_t now = arch_monotonic_ns();
  if (key_errors_logged && now - key_errors_logged_at < KEY_ERROR_LOG_INTERVAL_NS) {
    return;
  }
  klog("keyboard: too many keys pressed at once (%zu reports); those keys were not delivered\n",
       total);
  key_errors_logged = total;
  key_errors_logged_at = now;
}

bool ps2_keyboard_read_event(struct key_event *event)
{
  KASSERT(cpu_current() == cpu_bsp() && event);
  if (!available) {
    return false;
  }
  report_key_errors();
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

bool ps2_keyboard_sync_device(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return available && ps2_sync_input();
}

bool ps2_keyboard_input_complete(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return available && !raw_count && !input_lost && !extended && !released &&
      !pause_index && !pause_release && ps2_input_empty();
}
