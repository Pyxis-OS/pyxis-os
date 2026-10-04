#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/ps2.h>
#include <kernel/log.h>
#include <kernel/mouse.h>
#include <kernel/panic.h>

#define PS2_WRITE_AUXILIARY 0xd4
#define MOUSE_GET_DEVICE_ID 0xf2
#define MOUSE_SET_SAMPLE_RATE 0xf3
#define MOUSE_ENABLE_REPORTING 0xf4
#define MOUSE_SET_DEFAULTS 0xf6
#define MOUSE_RESET 0xff
#define MOUSE_ERROR 0xfc
#define MOUSE_SELF_TEST_PASSED 0xaa
#define MOUSE_ID_WHEEL 0x03
#define MOUSE_DEFAULT_SAMPLE_RATE 100
/* Linux libps2 bounds: Synaptics devices, like the ThinkPad's touchpad,
 * finish the reset before ACKing it, so reset needs the long bound. */
#define MOUSE_REPLY_TIMEOUT_NS UINT64_C(500000000)
#define MOUSE_RESET_TIMEOUT_NS UINT64_C(4000000000)
#define MOUSE_STANDARD_PACKET_BYTES 3
#define MOUSE_WHEEL_PACKET_BYTES 4
#define PACKET_LEFT (1u << 0)
#define PACKET_RIGHT (1u << 1)
#define PACKET_MIDDLE (1u << 2)
#define PACKET_ALWAYS_ONE (1u << 3)
#define PACKET_X_SIGN (1u << 4)
#define PACKET_Y_SIGN (1u << 5)
#define PACKET_X_OVERFLOW (1u << 6)
#define PACKET_Y_OVERFLOW (1u << 7)
#define PACKET_SIGN_EXTEND 0x100
#define RAW_QUEUE_BYTES 256

/* IntelliMouse wheel detection: these sample rates, then a device ID of 3. */
static const uint8_t wheel_probe_rates[] = {200, 100, 80};

static bool available;
static size_t packet_bytes = MOUSE_STANDARD_PACKET_BYTES;
/* IRQ producer and one task consumer, both on the BSP. Consumer accesses to
 * this queue disable interrupts; decoding state belongs solely to the task. */
static uint8_t raw_queue[RAW_QUEUE_BYTES];
static size_t raw_read, raw_write, raw_count;
static bool input_lost;
static uint8_t packet[MOUSE_WHEEL_PACKET_BYTES];
static size_t packet_index;
/* Diagnostic counts for debugger inspection; they never affect decoding. */
static uint64_t unsynchronized_bytes, overflowed_packets, lost_inputs;

static bool mouse_command_timeout(struct ps2_setup *setup, uint8_t command, uint64_t timeout_ns)
{
  for (unsigned attempt = 0; attempt < PS2_COMMAND_ATTEMPTS; ++attempt) {
    if (!ps2_write_command(setup, PS2_WRITE_AUXILIARY) || !ps2_write_data(setup, command)) {
      return false;
    }
    /* Firmware may have left reporting on; its packets can precede the ACK. */
    for (unsigned i = 0; i < PS2_DRAIN_LIMIT; ++i) {
      uint8_t reply;
      if (ps2_read_reply(setup, PS2_AUXILIARY_CHANNEL, &reply, timeout_ns) !=
          PS2_REPLY_RECEIVED || reply == MOUSE_ERROR) {
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

static bool mouse_command(struct ps2_setup *setup, uint8_t command)
{
  return mouse_command_timeout(setup, command, MOUSE_REPLY_TIMEOUT_NS);
}

static bool read_mouse_reply(struct ps2_setup *setup, uint8_t *reply, uint64_t timeout_ns)
{
  return ps2_read_reply(setup, PS2_AUXILIARY_CHANNEL, reply, timeout_ns) == PS2_REPLY_RECEIVED;
}

static bool set_sample_rate(struct ps2_setup *setup, uint8_t rate)
{
  return mouse_command(setup, MOUSE_SET_SAMPLE_RATE) && mouse_command(setup, rate);
}

bool mouse_configure(struct ps2_setup *setup)
{
  uint8_t reply, id;
  setup->step = "reset";
  if (!mouse_command_timeout(setup, MOUSE_RESET, MOUSE_RESET_TIMEOUT_NS)) {
    return false;
  }
  setup->step = "reset self-test";
  if (!read_mouse_reply(setup, &reply, MOUSE_RESET_TIMEOUT_NS) ||
      reply != MOUSE_SELF_TEST_PASSED) {
    return false;
  }
  setup->step = "reset device ID";
  if (!read_mouse_reply(setup, &id, MOUSE_REPLY_TIMEOUT_NS)) {
    return false;
  }
  setup->step = "set defaults";
  if (!mouse_command(setup, MOUSE_SET_DEFAULTS)) {
    return false;
  }

  setup->step = "wheel probe";
  for (size_t i = 0; i < sizeof(wheel_probe_rates); ++i) {
    if (!set_sample_rate(setup, wheel_probe_rates[i])) {
      return false;
    }
  }
  setup->step = "read device ID";
  if (!mouse_command(setup, MOUSE_GET_DEVICE_ID) ||
      !read_mouse_reply(setup, &id, MOUSE_REPLY_TIMEOUT_NS)) {
    return false;
  }
  setup->step = "restore sample rate";
  if (!set_sample_rate(setup, MOUSE_DEFAULT_SAMPLE_RATE)) {
    return false;
  }

  packet_bytes = id == MOUSE_ID_WHEEL ? MOUSE_WHEEL_PACKET_BYTES : MOUSE_STANDARD_PACKET_BYTES;
  klog("mouse: PS/2 device ID 0x%x, %s\n", id,
       id == MOUSE_ID_WHEEL ? "wheel, 4-byte packets" : "no wheel, 3-byte packets");
  return true;
}

bool mouse_enable_reporting(struct ps2_setup *setup)
{
  setup->step = "enable reporting";
  if (!mouse_command(setup, MOUSE_ENABLE_REPORTING)) {
    return false;
  }
  available = true;
  klog("mouse: PS/2 reporting enabled\n");
  return true;
}

void mouse_receive(uint8_t status, uint8_t data)
{
  if (!available) {
    return;
  }
  if ((status & (PS2_TIMEOUT_ERROR | PS2_PARITY_ERROR)) || raw_count == RAW_QUEUE_BYTES) {
    input_lost = true;
  }
  if (input_lost) {
    return;
  }
  raw_queue[raw_write] = data;
  raw_write = (raw_write + 1) % RAW_QUEUE_BYTES;
  ++raw_count;
}

bool mouse_available(void)
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

static int32_t packet_delta(uint8_t value, bool negative)
{
  return negative ? (int32_t)value - PACKET_SIGN_EXTEND : value;
}

static bool decode(uint8_t byte, struct mouse_event *event)
{
  /* Every first byte has bit 3 set; skipping others realigns a shifted stream. */
  if (!packet_index && !(byte & PACKET_ALWAYS_ONE)) {
    ++unsynchronized_bytes;
    return false;
  }
  packet[packet_index++] = byte;
  if (packet_index < packet_bytes) {
    return false;
  }
  packet_index = 0;

  uint8_t flags = packet[0];
  *event = (struct mouse_event){0};
  if (flags & PACKET_LEFT) {
    event->buttons |= MOUSE_BUTTON_LEFT;
  }
  if (flags & PACKET_RIGHT) {
    event->buttons |= MOUSE_BUTTON_RIGHT;
  }
  if (flags & PACKET_MIDDLE) {
    event->buttons |= MOUSE_BUTTON_MIDDLE;
  }
  /* An overflowed delta is meaningless, but buttons and wheel still apply. */
  if (flags & (PACKET_X_OVERFLOW | PACKET_Y_OVERFLOW)) {
    ++overflowed_packets;
  } else {
    event->dx = packet_delta(packet[1], flags & PACKET_X_SIGN);
    /* The device reports +Y as up. */
    event->dy = -packet_delta(packet[2], flags & PACKET_Y_SIGN);
  }
  if (packet_bytes == MOUSE_WHEEL_PACKET_BYTES) {
    event->wheel = (int8_t)packet[3];
  }
  return true;
}

bool mouse_read_event(struct mouse_event *event)
{
  KASSERT(cpu_current() == cpu_bsp() && event);
  if (!available) {
    return false;
  }

  for (;;) {
    uint8_t byte;
    switch (read_raw(&byte)) {
    case RAW_EMPTY:
      return false;
    case RAW_LOST:
      packet_index = 0;
      ++lost_inputs;
      *event = (struct mouse_event){.reset = true};
      return true;
    case RAW_BYTE:
      if (decode(byte, event)) {
        return true;
      }
      break;
    }
  }
}
