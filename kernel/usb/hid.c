#include <arch/cpu.h>
#include <arch/clock.h>
#include <abi/pointer.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <kernel/boot/options.h>
#include "hid.h"
#include "edge.h"

#define USB_CONFIGURATION_BYTES 9
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_DESCRIPTOR_HID 0x21
#define USB_DESCRIPTOR_REPORT 0x22
#define USB_CLASS_HID 3
#define USB_SUBCLASS_BOOT 1
#define USB_PROTOCOL_KEYBOARD 1
#define USB_PROTOCOL_MOUSE 2
/* Internal profile marker; the Edge's bInterfaceProtocol is zero. */
#define USB_PROTOCOL_EDGE 3
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_INTERRUPT 3
#define USB_ENDPOINT_TYPE_MASK 3
#define USB_PACKET_MASK 0x07ff
#define USB_PACKET_TRANSACTIONS_SHIFT 11
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_GET_REPORT 1
#define USB_REQUEST_SET_REPORT 9
#define USB_REQUEST_SET_IDLE 10
#define USB_REQUEST_SET_PROTOCOL 11
#define USB_REQUEST_GET_PROTOCOL 3
#define USB_REQUEST_INTERFACE_IN 0x81
#define USB_REQUEST_CLASS_INTERFACE_IN 0xa1
#define USB_REQUEST_CLASS_INTERFACE_OUT 0x21
#define USB_REPORT_INPUT 1
#define USB_REPORT_FEATURE 3
#define USB_MODIFIER_FIRST 0xe0
#define USB_KEY_ERROR_LAST 3
#define USB_QEMU_VENDOR 0x0627
#define USB_QEMU_PRODUCT 0x0001
#define USB_BOOT_MOUSE_WHEEL_BYTES 4
#define USB_QEMU_MOUSE_PACKET 4
#define USB_QEMU_MOUSE_DESCRIPTOR_BYTES 52
#define USB_QEMU_KEYBOARD_DESCRIPTOR_BYTES 63
#define USB_QEMU_KEYBOARD_PACKET 8
#define USB_QEMU_QUEUE_ENTRIES 16
#define USB_HID_MALFORMED_LIMIT 16
#define USB_HID_MALFORMED_DIAGNOSTIC_NS UINT64_C(5000000000)
#define USB_HID_MOUSE_SAMPLES 4
#define USB_HID_MOUSE_SAMPLE_BYTES 16

enum hid_stage {
  HID_CONFIGURATION, HID_KEYBOARD_DESCRIPTOR, HID_MOUSE_DESCRIPTOR, HID_EDGE_DESCRIPTOR, HID_ENDPOINTS,
  HID_PROTOCOL, HID_PROTOCOL_READBACK, HID_FEATURE_READ, HID_FEATURE_WRITE, HID_FEATURE_CONFIRM,
  HID_IDLE, HID_INITIAL, HID_START, HID_FINISHED,
};

static bool malformed_report(struct usb_hid_binding *binding,
    struct usb_hid_interface *interface, size_t bytes, bool decoded);
static bool decode_mouse_report(struct usb_hid_binding *binding,
    struct usb_hid_interface *interface, size_t bytes, struct usb_hid_mouse_sample *sample);

static bool diagnostics_enabled(void)
{
  const struct boot_options *options = boot_options_get();
  return options->usb_xhci_diagnose || options->usb_hid_describe;
}

static const enum key_code usage_keys[256] = {
  [0x04] = KEY_A, [0x05] = KEY_B, [0x06] = KEY_C, [0x07] = KEY_D,
  [0x08] = KEY_E, [0x09] = KEY_F, [0x0a] = KEY_G, [0x0b] = KEY_H,
  [0x0c] = KEY_I, [0x0d] = KEY_J, [0x0e] = KEY_K, [0x0f] = KEY_L,
  [0x10] = KEY_M, [0x11] = KEY_N, [0x12] = KEY_O, [0x13] = KEY_P,
  [0x14] = KEY_Q, [0x15] = KEY_R, [0x16] = KEY_S, [0x17] = KEY_T,
  [0x18] = KEY_U, [0x19] = KEY_V, [0x1a] = KEY_W, [0x1b] = KEY_X,
  [0x1c] = KEY_Y, [0x1d] = KEY_Z,
  [0x1e] = KEY_1, [0x1f] = KEY_2, [0x20] = KEY_3, [0x21] = KEY_4,
  [0x22] = KEY_5, [0x23] = KEY_6, [0x24] = KEY_7, [0x25] = KEY_8,
  [0x26] = KEY_9, [0x27] = KEY_0,
  [0x28] = KEY_ENTER, [0x29] = KEY_ESCAPE, [0x2a] = KEY_BACKSPACE,
  [0x2b] = KEY_TAB, [0x2c] = KEY_SPACE, [0x2d] = KEY_MINUS,
  [0x2e] = KEY_EQUAL, [0x2f] = KEY_LEFT_BRACKET, [0x30] = KEY_RIGHT_BRACKET,
  [0x31] = KEY_BACKSLASH, [0x32] = KEY_BACKSLASH,
  [0x33] = KEY_SEMICOLON, [0x34] = KEY_APOSTROPHE, [0x35] = KEY_GRAVE,
  [0x36] = KEY_COMMA, [0x37] = KEY_PERIOD, [0x38] = KEY_SLASH,
  [0x39] = KEY_CAPS_LOCK,
  [0x3a] = KEY_F1, [0x3b] = KEY_F2, [0x3c] = KEY_F3, [0x3d] = KEY_F4,
  [0x3e] = KEY_F5, [0x3f] = KEY_F6, [0x40] = KEY_F7, [0x41] = KEY_F8,
  [0x42] = KEY_F9, [0x43] = KEY_F10, [0x44] = KEY_F11, [0x45] = KEY_F12,
  [0x46] = KEY_PRINT_SCREEN, [0x47] = KEY_SCROLL_LOCK, [0x48] = KEY_PAUSE,
  [0x49] = KEY_INSERT, [0x4a] = KEY_HOME, [0x4b] = KEY_PAGE_UP,
  [0x4c] = KEY_DELETE, [0x4d] = KEY_END, [0x4e] = KEY_PAGE_DOWN,
  [0x4f] = KEY_RIGHT, [0x50] = KEY_LEFT, [0x51] = KEY_DOWN, [0x52] = KEY_UP,
  [0x53] = KEY_NUM_LOCK, [0x54] = KEY_KP_DIVIDE, [0x55] = KEY_KP_MULTIPLY,
  [0x56] = KEY_KP_MINUS, [0x57] = KEY_KP_PLUS, [0x58] = KEY_KP_ENTER,
  [0x59] = KEY_KP_1, [0x5a] = KEY_KP_2, [0x5b] = KEY_KP_3,
  [0x5c] = KEY_KP_4, [0x5d] = KEY_KP_5, [0x5e] = KEY_KP_6,
  [0x5f] = KEY_KP_7, [0x60] = KEY_KP_8, [0x61] = KEY_KP_9,
  [0x62] = KEY_KP_0, [0x63] = KEY_KP_PERIOD,
  [0x64] = KEY_NON_US_BACKSLASH, [0x65] = KEY_MENU,
  [0x72] = KEY_F23,
  [0xe0] = KEY_LEFT_CONTROL, [0xe1] = KEY_LEFT_SHIFT,
  [0xe2] = KEY_LEFT_ALT, [0xe3] = KEY_LEFT_SUPER,
  [0xe4] = KEY_RIGHT_CONTROL, [0xe5] = KEY_RIGHT_SHIFT,
  [0xe6] = KEY_RIGHT_ALT, [0xe7] = KEY_RIGHT_SUPER,
};

/* QEMU v10.2.2 hw/usb/dev-hid.c: one known layout, not a report parser. */
static const uint8_t qemu_mouse_descriptor[USB_QEMU_MOUSE_DESCRIPTOR_BYTES] = {
  0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
  0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03,
  0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
  0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
  0xc0, 0xc0,
};

/* QEMU v10.2.2 hw/usb/dev-hid.c: identify its buffered-snapshot keyboard. */
static const uint8_t qemu_keyboard_descriptor[USB_QEMU_KEYBOARD_DESCRIPTOR_BYTES] = {
  0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x75, 0x01, 0x95, 0x08,
  0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
  0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05,
  0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0xff, 0x05, 0x07,
  0x19, 0x00, 0x29, 0xff, 0x81, 0x00, 0xc0,
};

static uint16_t read16(const uint8_t *bytes)
{
  return bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void select_interface(struct usb_hid_binding *candidate,
                              const struct usb_hid_interface *interface,
                              unsigned inputs)
{
  if (!interface->protocol || inputs != 1 || !interface->descriptor_bytes) {
    return;
  }
  if (interface->protocol == USB_PROTOCOL_EDGE) {
    if (interface->number == USB_EDGE_INTERFACE &&
        interface->endpoint.address == USB_EDGE_ENDPOINT &&
        interface->endpoint.packet == USB_EDGE_REPORT_BYTES &&
        interface->endpoint.interval == USB_EDGE_INTERVAL && !interface->endpoint.transactions &&
        interface->descriptor_bytes == USB_EDGE_DESCRIPTOR_BYTES &&
        !candidate->interfaces[0].protocol) {
      candidate->interfaces[0] = *interface;
    }
    return;
  }
  if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
    if (!candidate->interfaces[0].protocol) {
      candidate->interfaces[0] = *interface;
    }
  } else if (candidate->candidate_count < USB_HID_MOUSE_CANDIDATES) {
    candidate->candidates[candidate->candidate_count++] = *interface;
  }
}

void usb_hid_select(struct usb_hid_binding *binding, const uint8_t *configuration,
                    size_t bytes, enum usb_speed speed, uint16_t vendor, uint16_t product,
                    uint16_t version)
{
  if (binding->configuration || (speed != USB_SPEED_LOW && speed != USB_SPEED_FULL &&
      speed != USB_SPEED_HIGH)) {
    return;
  }
  binding->configuration = configuration[5];
  binding->vendor = vendor;
  binding->product = product;
  binding->version = version;
  bool edge = vendor == USB_EDGE_VENDOR && product == USB_EDGE_PRODUCT;
  struct usb_hid_interface interface = {0};
  unsigned inputs = 0;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < bytes; offset += configuration[offset]) {
    const uint8_t *part = configuration + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      select_interface(binding, &interface, inputs);
      interface = (struct usb_hid_interface){0};
      inputs = 0;
      bool boot = part[6] == USB_SUBCLASS_BOOT;
      if (!part[3] && part[5] == USB_CLASS_HID &&
          (!edge || (part[2] == USB_EDGE_INTERFACE && !part[6] && !part[7])) &&
          ((boot && (part[7] == USB_PROTOCOL_KEYBOARD || part[7] == USB_PROTOCOL_MOUSE)) ||
           (!part[6] && !part[7]))) {
        interface.number = part[2];
        interface.boot = boot;
        interface.protocol = edge && !boot ? USB_PROTOCOL_EDGE :
          boot ? part[7] : USB_PROTOCOL_MOUSE;
      }
    } else if (interface.protocol && part[1] == USB_DESCRIPTOR_HID && part[0] >= 9 &&
               part[5] && part[0] == 6 + 3 * part[5]) {
      for (unsigned entry = 0; entry < part[5]; ++entry) {
        if (part[6 + entry * 3] == USB_DESCRIPTOR_REPORT) {
          interface.descriptor_bytes = read16(part + 7 + entry * 3);
        }
      }
    } else if (interface.protocol && part[1] == USB_DESCRIPTOR_ENDPOINT &&
               (part[2] & USB_ENDPOINT_IN) &&
               (part[3] & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_INTERRUPT) {
      ++inputs;
      uint16_t wire_packet = read16(part + 4);
      interface.endpoint = (struct usb_interrupt_endpoint){
        .address = part[2], .packet = wire_packet & USB_PACKET_MASK, .interval = part[6],
        .transactions = (wire_packet >> USB_PACKET_TRANSACTIONS_SHIFT) & 3,
      };
      unsigned minimum = interface.protocol == USB_PROTOCOL_KEYBOARD ?
        USB_HID_KEYBOARD_BYTES : USB_HID_MOUSE_BYTES;
      interface.receive_bytes = interface.endpoint.packet > minimum ? interface.endpoint.packet : minimum;
    }
  }
  select_interface(binding, &interface, inputs);
  if (!binding->interfaces[0].protocol && !binding->candidate_count) {
    binding->configuration = 0;
    return;
  }
  struct usb_hid_interface *keyboard = &binding->interfaces[0];
  keyboard->qemu_keyboard_candidate = keyboard->protocol && vendor == USB_QEMU_VENDOR &&
    product == USB_QEMU_PRODUCT && binding->configuration == 1 && !keyboard->number &&
    keyboard->endpoint.address == 0x81 && keyboard->endpoint.packet == USB_QEMU_KEYBOARD_PACKET &&
    !keyboard->endpoint.transactions &&
    keyboard->descriptor_bytes == USB_QEMU_KEYBOARD_DESCRIPTOR_BYTES;
  for (unsigned index = 0; index < binding->candidate_count; ++index) {
    struct usb_hid_interface *mouse = &binding->candidates[index];
    mouse->qemu_wheel_candidate = mouse->boot && vendor == USB_QEMU_VENDOR &&
      product == USB_QEMU_PRODUCT && binding->configuration == 1 && !mouse->number &&
      mouse->endpoint.address == 0x81 && mouse->endpoint.packet == USB_QEMU_MOUSE_PACKET &&
      !mouse->endpoint.transactions && mouse->descriptor_bytes == USB_QEMU_MOUSE_DESCRIPTOR_BYTES;
  }
}

static bool keyboard_snapshot(const uint8_t *report, size_t bytes, bool keys[KEY_COUNT],
                               bool *unresolved)
{
  *unresolved = false;
  if (bytes != USB_HID_KEYBOARD_BYTES) {
    return false;
  }
  memset(keys, 0, KEY_COUNT * sizeof(*keys));
  for (unsigned bit = 0; bit < 8; ++bit) {
    if (report[0] & (1u << bit)) {
      keys[usage_keys[USB_MODIFIER_FIRST + bit]] = true;
    }
  }
  for (unsigned index = 2; index < USB_HID_KEYBOARD_BYTES; ++index) {
    uint8_t usage = report[index];
    if (usage && usage <= USB_KEY_ERROR_LAST) {
      *unresolved = true;
    } else if (usage_keys[usage] != KEY_NONE) {
      keys[usage_keys[usage]] = true;
    }
  }
  return true;
}

void usb_hid_lost(struct usb_hid_binding *binding)
{
  if (!binding->claimed || binding->lost) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  if (binding->active) {
    if (binding->interfaces[0].protocol == USB_PROTOCOL_EDGE) {
      gamepad_source_lost(&binding->gamepad);
    } else {
      input_source_lost(&binding->source);
    }
  }
  binding->active = false;
  binding->lost = true;
  binding->retirement_pending = true;
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    if (binding->interfaces[index].stream) {
      usb_host_interrupt_ack_loss(binding->interfaces[index].stream);
    }
  }
  cpu_restore_interrupts(flags);
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    struct usb_hid_interface *interface = &binding->interfaces[index];
    if (interface->stream) {
      usb_host_interrupt_diagnose_loss(interface->stream, interface->number);
    }
  }
  kdebug("usb HID: source detached\n");
}

static enum usb_result bind_failed(struct usb_hid_binding *binding, enum usb_result result)
{
  if (binding->ticket_active) {
    usb_host_control_abandon(binding->host, binding->ticket);
    binding->ticket_active = false;
  }
  binding->result = result;
  binding->stage = HID_FINISHED;
  usb_hid_lost(binding);
  return result;
}

enum usb_result usb_hid_begin(struct usb_hid_binding *binding,
                              struct usb_host_device *device, uint64_t deadline)
{
  if (!binding->configuration || binding->claimed) {
    return USB_UNSUPPORTED;
  }
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    if (binding->interfaces[index].receive_bytes > usb_host_hid_interrupt_capacity()) {
      return USB_UNSUPPORTED;
    }
  }
  enum usb_result result = usb_host_hid_claim(device);
  if (result != USB_OK) {
    return result;
  }
  binding->host = device;
  binding->claimed = true;
  binding->deadline = deadline;
  binding->result = USB_BUSY;
  return USB_OK;
}

static void next_stage(struct usb_hid_binding *binding)
{
  ++binding->stage;
  binding->interface = binding->layout = 0;
}

static void mouse_fallback(struct usb_hid_binding *binding,
                            const struct usb_hid_interface *candidate)
{
  if (!binding->interfaces[1].protocol && candidate->boot) {
    binding->interfaces[1] = *candidate;
    binding->interfaces[1].receive_bytes = candidate->endpoint.packet;
    if (binding->interfaces[1].receive_bytes < USB_HID_MOUSE_BYTES) {
      binding->interfaces[1].receive_bytes = USB_HID_MOUSE_BYTES;
    }
  }
}

static bool boot_mouse_sample(const struct usb_hid_interface *interface,
                               const uint8_t *report, size_t bytes,
                               struct usb_hid_mouse_sample *sample)
{
  if (bytes < USB_HID_MOUSE_BYTES ||
      (interface->qemu_wheel && bytes != USB_QEMU_MOUSE_PACKET)) {
    return false;
  }
  bool wheel = bytes >= USB_BOOT_MOUSE_WHEEL_BYTES &&
      (interface->qemu_wheel || interface->reserved_id_boot);
  *sample = (struct usb_hid_mouse_sample){
    .x = (int8_t)report[1], .y = (int8_t)report[2],
    .wheel = wheel ? -(int8_t)report[3] : 0,
    .buttons = report[0] & (interface->qemu_wheel ? 31u : 7u),
  };
  return true;
}

static void mouse_wire_protocol(struct usb_hid_binding *binding,
                                  struct usb_hid_interface *interface, size_t bytes)
{
  if (!interface->boot || interface->protocol != USB_PROTOCOL_MOUSE ||
      !interface->report_protocol || !binding->mouse.ids || !bytes || binding->control[0]) {
    return;
  }
  /* ID 0 is reserved in an ID-bearing descriptor. Keep the observed format;
   * another SET_PROTOCOL would alter a live stream that already speaks boot. */
  interface->report_protocol = false;
  interface->reserved_id_boot = true;
  if (binding->active) {
    uint64_t flags = cpu_save_interrupts();
    input_source_pointer_configure(&binding->source, INPUT_POINTER_MOUSE,
        POINTER_SCROLL_UNITS_PER_DETENT, 1, POINTER_SCROLL_UNITS_PER_DETENT, 1);
    cpu_restore_interrupts(flags);
  }
  if (diagnostics_enabled()) {
    kinfo("usb HID diag: %x:%x interface=%u action=reserved-ID-boot bytes=%zu\n",
        binding->vendor, binding->product, interface->number, bytes);
  }
}

static struct usb_setup bind_request(const struct usb_hid_binding *binding)
{
  const struct usb_hid_interface *interface = &binding->interfaces[binding->interface];
  struct usb_setup setup = {
    .request_type = USB_REQUEST_CLASS_INTERFACE_OUT,
    .index = interface->number,
  };
  switch (binding->stage) {
  case HID_KEYBOARD_DESCRIPTOR: case HID_EDGE_DESCRIPTOR:
    setup.request_type = USB_REQUEST_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_DESCRIPTOR;
    setup.value = USB_DESCRIPTOR_REPORT << 8;
    setup.length = interface->descriptor_bytes;
    break;
  case HID_MOUSE_DESCRIPTOR:
    interface = &binding->candidates[binding->candidate];
    setup.index = interface->number;
    setup.request_type = USB_REQUEST_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_DESCRIPTOR;
    setup.value = USB_DESCRIPTOR_REPORT << 8;
    setup.length = interface->descriptor_bytes;
    break;
  case HID_CONFIGURATION:
    setup = (struct usb_setup){.request = USB_REQUEST_SET_CONFIGURATION,
                              .value = binding->configuration};
    break;
  case HID_PROTOCOL:
    setup.request = USB_REQUEST_SET_PROTOCOL;
    setup.value = interface->report_protocol ? 1 : 0;
    break;
  case HID_PROTOCOL_READBACK:
    setup.request_type = USB_REQUEST_CLASS_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_PROTOCOL;
    setup.length = 1;
    break;
  case HID_IDLE:
    setup.request = USB_REQUEST_SET_IDLE;
    break;
  case HID_INITIAL:
    setup.request_type = USB_REQUEST_CLASS_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_REPORT;
    setup.value = USB_REPORT_INPUT << 8;
    setup.length = interface->receive_bytes;
    if (interface->report_protocol) {
      setup.value |= binding->mouse.layouts[binding->layout].id;
      setup.length = usb_hid_report_bytes(&binding->mouse, binding->layout, HID_INPUT);
    }
    break;
  case HID_FEATURE_READ: case HID_FEATURE_WRITE: case HID_FEATURE_CONFIRM:
    setup.index = binding->interfaces[1].number;
    setup.request_type = binding->stage == HID_FEATURE_WRITE ?
      USB_REQUEST_CLASS_INTERFACE_OUT : USB_REQUEST_CLASS_INTERFACE_IN;
    setup.request = binding->stage == HID_FEATURE_WRITE ? USB_REQUEST_SET_REPORT : USB_REQUEST_GET_REPORT;
    setup.value = (USB_REPORT_FEATURE << 8) | binding->mouse.layouts[binding->layout].id;
    setup.length = usb_hid_report_bytes(&binding->mouse, binding->layout, HID_FEATURE);
    break;
  default:
    break;
  }
  return setup;
}

static bool feature_stage(unsigned stage)
{
  return stage >= HID_FEATURE_READ && stage <= HID_FEATURE_CONFIRM;
}

static void next_feature(struct usb_hid_binding *binding)
{
  binding->stage = HID_FEATURE_READ;
  ++binding->layout;
}

static void disable_feature(struct usb_hid_binding *binding)
{
  usb_hid_feature_disable(&binding->mouse, binding->layout);
  kdebug("usb HID: uncertain wheel multiplier on interface %u report %u; affected scroll disabled\n",
         binding->interfaces[1].number, binding->mouse.layouts[binding->layout].id);
  next_feature(binding);
}

static void describe_descriptor(const struct usb_hid_binding *binding,
                                 const struct usb_hid_interface *interface,
                                 const struct usb_completion *completion)
{
  if (!boot_options_get()->usb_hid_describe) {
    return;
  }
  kinfo("usb HID describe: %x:%x configuration %u interface %u endpoint %x boot %u descriptor %zu/%u result %u\n",
        binding->vendor, binding->product, binding->configuration, interface->number,
        interface->endpoint.address, interface->boot, completion->bytes,
        interface->descriptor_bytes, (unsigned)completion->result);
  static const char digits[] = "0123456789abcdef";
  for (size_t offset = 0; offset < completion->bytes; offset += 16) {
    char line[16 * 3 + 1];
    unsigned length = 0;
    for (size_t index = offset; index < completion->bytes && index < offset + 16; ++index) {
      uint8_t byte = binding->control[index];
      line[length++] = digits[byte >> 4];
      line[length++] = digits[byte & 15];
      line[length++] = ' ';
    }
    line[length] = 0;
    kinfo("usb HID descriptor %x:%x interface %u +%zx: %s\n",
          binding->vendor, binding->product, interface->number, offset, line);
  }
}

static void describe_binding(const struct usb_hid_binding *binding)
{
  if (!boot_options_get()->usb_hid_describe) {
    return;
  }
  const struct usb_hid_interface *mouse = &binding->interfaces[1];
  kinfo("usb HID describe: %x:%x keyboard %u mouse %u interface %u protocol %s receive %u pan 120/%u enabled %u wheel 120/%u enabled %u\n",
        binding->vendor, binding->product, binding->interfaces[0].protocol != 0,
        mouse->protocol != 0, mouse->number, mouse->report_protocol ? "report" : "boot",
        mouse->receive_bytes, mouse->report_protocol ? binding->mouse.pan_multiplier : 1,
        mouse->protocol && mouse->report_protocol && binding->mouse.pan_enabled,
        mouse->report_protocol ? binding->mouse.wheel_multiplier : 1,
        mouse->protocol && (mouse->report_protocol ? binding->mouse.wheel_enabled :
         (mouse->qemu_wheel || mouse->reserved_id_boot)));
  if (!mouse->protocol || !mouse->report_protocol) {
    return;
  }
  for (unsigned index = 0; index < binding->mouse.layout_count; ++index) {
    const struct usb_hid_report_layout *layout = &binding->mouse.layouts[index];
    kinfo("usb HID layout: interface %u ID %u input %zu output %zu feature %zu pointer %u buttons %u\n",
          mouse->number, layout->id, usb_hid_report_bytes(&binding->mouse, index, HID_INPUT),
          usb_hid_report_bytes(&binding->mouse, index, HID_OUTPUT),
          usb_hid_report_bytes(&binding->mouse, index, HID_FEATURE), layout->pointer, layout->button_fields);
  }
  static const char *const names[] = {"x", "y", "wheel", "pan", "button", "button-array", "multiplier"};
  for (unsigned index = 0; index < binding->mouse.field_count; ++index) {
    const struct usb_hid_report_field *field = &binding->mouse.fields[index];
    kinfo("usb HID field: ID %u %s offset %u bits %u range %lld..%lld scope %u button %u multiplier %u\n",
          binding->mouse.layouts[field->layout].id, names[field->kind], field->offset,
          field->size, (long long)field->minimum, (long long)field->maximum,
          field->scope, field->button, field->multiplier);
  }
}

static enum usb_result start_binding(struct usb_hid_binding *binding)
{
  if (!binding->interfaces[0].protocol && !binding->interfaces[1].protocol) {
    return bind_failed(binding, USB_UNSUPPORTED);
  }
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    struct usb_hid_interface *interface = &binding->interfaces[index];
    if (interface->protocol) {
      enum usb_result result = usb_host_hid_interrupt_length(interface->stream, interface->receive_bytes);
      if (result != USB_OK) {
        return bind_failed(binding, result);
      }
    }
  }
  uint64_t flags = cpu_save_interrupts();
  const struct usb_hid_interface *mouse = &binding->interfaces[1];
  bool edge = binding->interfaces[0].protocol == USB_PROTOCOL_EDGE;
  if (edge) {
    if (!gamepad_source_attach(&binding->gamepad, binding->vendor, binding->product,
        binding->version)) {
      cpu_restore_interrupts(flags);
      return bind_failed(binding, USB_UNSUPPORTED);
    }
  } else {
    if (mouse->protocol) {
      input_source_pointer_configure(&binding->source, INPUT_POINTER_MOUSE,
          POINTER_SCROLL_UNITS_PER_DETENT, mouse->report_protocol ? binding->mouse.pan_multiplier : 1,
          POINTER_SCROLL_UNITS_PER_DETENT, mouse->report_protocol ? binding->mouse.wheel_multiplier : 1);
    }
    input_source_attach(&binding->source, binding->interfaces[0].protocol != 0, mouse->protocol != 0);
    if (binding->interfaces[0].protocol) {
      input_source_initial_keyboard(&binding->source, binding->initial_keys);
    }
    if (mouse->protocol) {
      input_source_initial_pointer(&binding->source, binding->initial_buttons);
    }
  }
  binding->active = true;
  cpu_restore_interrupts(flags);
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    struct usb_hid_interface *interface = &binding->interfaces[index];
    if (interface->protocol) {
      enum usb_result result = usb_host_interrupt_start(interface->stream);
      if (result != USB_OK) {
        return bind_failed(binding, result);
      }
    }
  }
  binding->result = USB_OK;
  binding->stage = HID_FINISHED;
  if (edge) {
    kdebug("usb HID: DualSense Edge USB source attached\n");
    return USB_OK;
  }
  describe_binding(binding);
  kdebug("usb HID: %s%s%s source attached\n",
         binding->interfaces[0].protocol ? "boot keyboard" : "",
         mouse->protocol ? (mouse->report_protocol ? "/report mouse" : "/boot mouse") : "",
         mouse->qemu_wheel ? " (QEMU wheel)" : "");
  return USB_OK;
}

static enum usb_result initial_snapshot(struct usb_hid_binding *binding,
                                         const struct usb_completion *completion)
{
  struct usb_hid_interface *interface = &binding->interfaces[binding->interface];
  if (completion->result == USB_STALL) {
    /* EP0 has been fenced before collection. Unknown attachment holds begin
     * empty; only this report's state is affected when IDs are present. */
    if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
      memset(binding->initial_keys, 0, sizeof(binding->initial_keys));
    } else if (interface->report_protocol) {
      binding->mouse.layouts[binding->layout].buttons = 0;
      binding->initial_buttons = usb_hid_mouse_buttons(&binding->mouse);
    } else {
      binding->initial_buttons = 0;
    }
  } else if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
    bool unresolved;
    if (!keyboard_snapshot(binding->control, completion->bytes, binding->initial_keys, &unresolved)) {
      if (malformed_report(binding, interface, completion->bytes, false)) {
        return bind_failed(binding, USB_IO);
      }
      if (interface->qemu_keyboard) {
        return USB_BUSY;
      }
      ++binding->interface;
      return USB_BUSY;
    }
    interface->malformed_consecutive = 0;
    /* QEMU consumes scan bytes, including invisible prefixes, one GET at a
     * time. Preserve its established sixteen-snapshot attachment drain. */
    if (interface->qemu_keyboard && ++binding->initial_observations < USB_QEMU_QUEUE_ENTRIES) {
      return USB_BUSY;
    }
    if (unresolved) {
      return bind_failed(binding, USB_IO);
    }
  } else {
    struct usb_hid_mouse_sample sample;
    mouse_wire_protocol(binding, interface, completion->bytes);
    if (interface->report_protocol && binding->mouse.ids &&
        (!completion->bytes || binding->control[0] != binding->mouse.layouts[binding->layout].id)) {
      binding->mouse.decode_failure = HID_DECODE_ID;
    }
    bool valid = interface->report_protocol ?
      ((!binding->mouse.ids || (completion->bytes &&
        binding->control[0] == binding->mouse.layouts[binding->layout].id)) &&
       decode_mouse_report(binding, interface, completion->bytes, &sample)) :
      boot_mouse_sample(interface, binding->control, completion->bytes, &sample);
    if (!valid) {
      if (malformed_report(binding, interface, completion->bytes, true)) {
        return bind_failed(binding, USB_IO);
      }
      if (interface->qemu_wheel) {
        return USB_BUSY;
      }
      if (interface->report_protocol) {
        ++binding->layout;
      } else {
        ++binding->interface;
      }
      return USB_BUSY;
    }
    interface->malformed_consecutive = 0;
    binding->initial_buttons = sample.buttons;
    /* Nonzero QEMU deltas can retain its FIFO head. Drain sixteen zero-motion
     * observations before publishing the final button snapshot. */
    if (interface->qemu_wheel) {
      if (!sample.x && !sample.y && !sample.wheel && !sample.pan) {
        ++binding->initial_observations;
      }
      if (binding->initial_observations < USB_QEMU_QUEUE_ENTRIES) {
        return USB_BUSY;
      }
    }
  }
  binding->initial_observations = 0;
  if (interface->report_protocol) {
    ++binding->layout;
  } else {
    ++binding->interface;
  }
  return USB_BUSY;
}

enum usb_result usb_hid_bind_step(struct usb_hid_binding *binding)
{
  if (binding->stage == HID_FINISHED) {
    return binding->result;
  }
  if (!binding->claimed || !usb_host_device_present(binding->host)) {
    return bind_failed(binding, USB_IO);
  }
  if (task_deadline_expired(binding->deadline)) {
    return bind_failed(binding, USB_TIMEOUT);
  }
  if (binding->stage == HID_KEYBOARD_DESCRIPTOR &&
      !binding->interfaces[0].qemu_keyboard_candidate) {
    next_stage(binding);
    return USB_BUSY;
  }
  if (binding->stage == HID_MOUSE_DESCRIPTOR) {
    if (binding->candidate == binding->candidate_count) {
      next_stage(binding);
      return USB_BUSY;
    }
    struct usb_hid_interface *candidate = &binding->candidates[binding->candidate];
    if (candidate->descriptor_bytes > usb_host_control_capacity()) {
      mouse_fallback(binding, candidate);
      ++binding->candidate;
      return USB_BUSY;
    }
  }
  if (binding->stage == HID_EDGE_DESCRIPTOR &&
      binding->interfaces[0].protocol != USB_PROTOCOL_EDGE) {
    next_stage(binding);
    return USB_BUSY;
  }
  if (binding->stage == HID_ENDPOINTS) {
    if (binding->interface == USB_HID_ENDPOINTS_PER_DEVICE) {
      next_stage(binding);
      return USB_BUSY;
    }
    struct usb_hid_interface *interface = &binding->interfaces[binding->interface++];
    if (interface->protocol) {
      enum usb_result result = usb_host_configure_interrupt_in(binding->host,
          &interface->endpoint, interface->receive_bytes, USB_INTERRUPT_HID,
          binding->deadline, &interface->stream);
      if (result != USB_OK) {
        return bind_failed(binding, result);
      }
    }
    return USB_BUSY;
  }
  if (feature_stage(binding->stage)) {
    if (!binding->interfaces[1].protocol || !binding->interfaces[1].report_protocol) {
      binding->stage = HID_IDLE;
      binding->interface = binding->layout = 0;
      return USB_BUSY;
    }
    if (binding->stage == HID_FEATURE_READ) {
      while (binding->layout < binding->mouse.layout_count &&
             !usb_hid_feature_layout(&binding->mouse, binding->layout)) {
        ++binding->layout;
      }
      if (binding->layout == binding->mouse.layout_count) {
        binding->stage = HID_IDLE;
        binding->interface = binding->layout = 0;
        return USB_BUSY;
      }
    }
  } else if (binding->stage == HID_PROTOCOL || binding->stage == HID_PROTOCOL_READBACK || binding->stage == HID_IDLE ||
             binding->stage == HID_INITIAL) {
    while (binding->interface < USB_HID_ENDPOINTS_PER_DEVICE &&
           (!binding->interfaces[binding->interface].protocol ||
            binding->interfaces[binding->interface].protocol == USB_PROTOCOL_EDGE ||
            (binding->stage == HID_PROTOCOL && !binding->interfaces[binding->interface].boot) ||
            (binding->stage == HID_PROTOCOL_READBACK &&
             (!binding->interfaces[binding->interface].boot ||
              !binding->interfaces[binding->interface].report_protocol)))) {
      ++binding->interface;
    }
    if (binding->interface == USB_HID_ENDPOINTS_PER_DEVICE) {
      next_stage(binding);
      return USB_BUSY;
    }
    if (binding->stage == HID_INITIAL && binding->interfaces[binding->interface].report_protocol) {
      while (binding->layout < binding->mouse.layout_count &&
             !binding->mouse.layouts[binding->layout].button_fields &&
             !binding->interfaces[binding->interface].qemu_wheel) {
        ++binding->layout;
      }
      if (binding->layout == binding->mouse.layout_count) {
        ++binding->interface;
        return USB_BUSY;
      }
    }
  }
  if (binding->stage == HID_START) {
    return start_binding(binding);
  }
  struct usb_setup setup = bind_request(binding);
  if (!binding->ticket_active) {
    uint64_t deadline = task_deadline_after_ms(USB_CONTROL_TIMEOUT_MS);
    if (deadline > binding->deadline) {
      deadline = binding->deadline;
    }
    const void *outbound = binding->stage == HID_FEATURE_WRITE ? binding->control : NULL;
    enum usb_result result = usb_host_control_submit(binding->host, &setup, outbound,
                                                    deadline, &binding->ticket);
    if (result != USB_OK) {
      return bind_failed(binding, result);
    }
    binding->ticket_active = true;
    return USB_BUSY;
  }
  enum usb_result result = usb_host_control_poll(binding->host, binding->ticket, binding->deadline);
  if (result == USB_BUSY) {
    return USB_BUSY;
  }
  if (result != USB_OK) {
    return bind_failed(binding, result);
  }
  struct usb_completion completion;
  result = usb_host_control_take(binding->host, binding->ticket, binding->control,
                                 sizeof(binding->control), &completion);
  if (result != USB_OK) {
    return bind_failed(binding, result);
  }
  binding->ticket_active = false;
  struct usb_hid_interface *interface = &binding->interfaces[binding->interface];
  bool optional_stall = completion.result == USB_STALL &&
      ((binding->stage == HID_IDLE && interface->protocol == USB_PROTOCOL_MOUSE) ||
       binding->stage == HID_KEYBOARD_DESCRIPTOR || binding->stage == HID_MOUSE_DESCRIPTOR ||
       binding->stage == HID_INITIAL || binding->stage == HID_PROTOCOL_READBACK || feature_stage(binding->stage) ||
       (binding->stage == HID_PROTOCOL && interface->protocol == USB_PROTOCOL_MOUSE));
  if ((completion.result != USB_OK && !optional_stall) || (!setup.length && completion.bytes)) {
    return bind_failed(binding, completion.result == USB_OK ? USB_IO : completion.result);
  }
  switch (binding->stage) {
  case HID_CONFIGURATION:
    next_stage(binding);
    break;
  case HID_KEYBOARD_DESCRIPTOR:
    interface->qemu_keyboard = completion.result == USB_OK &&
      completion.bytes == sizeof(qemu_keyboard_descriptor) &&
      !memcmp(binding->control, qemu_keyboard_descriptor, sizeof(qemu_keyboard_descriptor));
    next_stage(binding);
    break;
  case HID_EDGE_DESCRIPTOR:
    describe_descriptor(binding, interface, &completion);
    if (completion.result != USB_OK || completion.bytes != interface->descriptor_bytes ||
        !usb_edge_descriptor(binding->control, completion.bytes)) {
      return bind_failed(binding, USB_UNSUPPORTED);
    }
    next_stage(binding);
    break;
  case HID_MOUSE_DESCRIPTOR: {
    struct usb_hid_interface *candidate = &binding->candidates[binding->candidate];
    describe_descriptor(binding, candidate, &completion);
    candidate->qemu_wheel = completion.result == USB_OK && candidate->qemu_wheel_candidate &&
      completion.bytes == sizeof(qemu_mouse_descriptor) &&
      !memcmp(binding->control, qemu_mouse_descriptor, sizeof(qemu_mouse_descriptor));
    if (completion.result == USB_OK && completion.bytes == candidate->descriptor_bytes &&
        usb_hid_mouse_parse(&binding->mouse, binding->control, completion.bytes)) {
      binding->interfaces[1] = *candidate;
      binding->interfaces[1].report_protocol = true;
      binding->interfaces[1].receive_bytes = candidate->endpoint.packet > binding->mouse.input_bytes ?
        candidate->endpoint.packet : binding->mouse.input_bytes;
      binding->candidate = binding->candidate_count;
    } else {
      mouse_fallback(binding, candidate);
      kdebug("usb HID: unsupported mouse descriptor on interface %u%s\n", candidate->number,
             candidate->boot ? "; boot fallback requires protocol confirmation" : "");
      ++binding->candidate;
    }
    break;
  }
  case HID_PROTOCOL:
    if (completion.result == USB_STALL) {
      if (interface->report_protocol) {
        interface->report_protocol = false;
        interface->receive_bytes = interface->endpoint.packet;
        if (interface->receive_bytes < USB_HID_MOUSE_BYTES) {
          interface->receive_bytes = USB_HID_MOUSE_BYTES;
        }
        kdebug("usb HID: report protocol refused on interface %u; confirming boot fallback\n", interface->number);
        return USB_BUSY;
      }
      kdebug("usb HID: boot protocol refused on mouse interface %u; mouse unavailable\n", interface->number);
      interface->protocol = 0;
    }
    ++binding->interface;
    break;
  case HID_PROTOCOL_READBACK:
    if (diagnostics_enabled()) {
      kinfo("usb HID diag: %x:%x interface=%u GET_PROTOCOL result=%u bytes=%zu selected=%d expected=1\n",
          binding->vendor, binding->product, interface->number, completion.result,
          completion.bytes, completion.result == USB_OK && completion.bytes == 1 ? binding->control[0] : -1);
    }
    /* Readback is evidence, not permission to reinterpret queued Input data. */
    ++binding->interface;
    break;
  case HID_FEATURE_READ: {
    uint32_t pan, wheel;
    if (completion.result == USB_STALL) {
      disable_feature(binding);
    } else {
      bool confirmed = usb_hid_feature_confirm(&binding->mouse, binding->layout, binding->control,
                                               completion.bytes, &pan, &wheel);
      if (usb_hid_feature_prepare(&binding->mouse, binding->layout, binding->control, completion.bytes)) {
        binding->stage = HID_FEATURE_WRITE;
      } else if (confirmed) {
        kdebug("usb HID: multiplier encoding not enabled on interface %u report %u; confirmed scale retained\n",
               binding->interfaces[1].number, binding->mouse.layouts[binding->layout].id);
        next_feature(binding);
      } else {
        disable_feature(binding);
      }
    }
    break;
  }
  case HID_FEATURE_WRITE:
    if (completion.result == USB_OK && completion.bytes != setup.length) {
      disable_feature(binding);
    } else {
      /* Even a refused write is followed by readback. The previous value is
       * not a safe assumption after a device may have seen the request. */
      binding->stage = HID_FEATURE_CONFIRM;
    }
    break;
  case HID_FEATURE_CONFIRM: {
    uint32_t pan, wheel;
    if (completion.result == USB_STALL ||
        !usb_hid_feature_confirm(&binding->mouse, binding->layout, binding->control,
                                 completion.bytes, &pan, &wheel)) {
      disable_feature(binding);
    } else {
      kdebug("usb HID: wheel multiplier readback interface %u report %u, pan %u wheel %u\n",
             binding->interfaces[1].number, binding->mouse.layouts[binding->layout].id, pan, wheel);
      next_feature(binding);
    }
    break;
  }
  case HID_IDLE:
    ++binding->interface;
    break;
  case HID_INITIAL:
    return initial_snapshot(binding, &completion);
  default:
    return bind_failed(binding, USB_INVALID);
  }
  return USB_BUSY;
}

enum usb_result usb_hid_bind_boot(struct usb_hid_binding *binding,
                                  struct usb_host_device *device, uint64_t deadline)
{
  enum usb_result result = usb_hid_begin(binding, device, deadline);
  if (result != USB_OK) {
    return result;
  }
  while ((result = usb_hid_bind_step(binding)) == USB_BUSY) {
    if (binding->ticket_active) {
      result = usb_host_control_wait(binding->host, binding->ticket, deadline);
      if (result != USB_OK) {
        return bind_failed(binding, result);
      }
    }
  }
  return result;
}

static void diagnose_report(struct usb_hid_binding *binding, struct usb_hid_interface *interface,
    const char *action, enum usb_result result, size_t bytes, bool decoded)
{
  if (!diagnostics_enabled()) {
    return;
  }
  kinfo("usb HID diag: %x:%x interface=%u protocol=%s action=%s result=%u bytes=%zu "
      "malformed=%llu consecutive=%u\n",
      binding->vendor, binding->product, interface->number,
      interface->report_protocol ? "report" : "boot", action, result, bytes,
      (unsigned long long)interface->malformed_reports, interface->malformed_consecutive);
  if (!decoded || !interface->report_protocol) {
    return;
  }
  const struct usb_hid_mouse_reports *mouse = &binding->mouse;
  unsigned id = bytes && mouse->ids ? binding->control[0] : 0;
  size_t expected = 0;
  for (unsigned i = 0; i < mouse->layout_count; ++i) {
    if (mouse->layouts[i].id == id) {
      expected = usb_hid_report_bytes(mouse, i, HID_INPUT);
      break;
    }
  }
  kinfo("usb HID diag: decode-failure=%u ID=%u expected=%zu\n",
      mouse->decode_failure, id, expected);
  if (mouse->decode_failure == HID_DECODE_RANGE || mouse->decode_failure == HID_DECODE_ARRAY) {
    const struct usb_hid_report_field *field = &mouse->fields[mouse->decode_field];
    kinfo("usb HID diag: field=%u kind=%u offset=%u bits=%u value=%lld range=%lld..%lld\n",
        mouse->decode_field, field->kind, field->offset, field->size,
        (long long)mouse->decode_value, (long long)field->minimum, (long long)field->maximum);
  }
}

static bool decode_mouse_report(struct usb_hid_binding *binding,
    struct usb_hid_interface *interface, size_t bytes, struct usb_hid_mouse_sample *sample)
{
  const struct usb_hid_mouse_reports *mouse = &binding->mouse;
  if (diagnostics_enabled() && (bytes || !mouse->ids)) {
    unsigned id = mouse->ids ? binding->control[0] : 0;
    for (unsigned i = 0; i < mouse->layout_count; ++i) {
      size_t expected = usb_hid_report_bytes(mouse, i, HID_INPUT);
      if (mouse->layouts[i].id == id && expected && bytes != expected &&
          !interface->length_diagnosed[i]) {
        interface->length_diagnosed[i] = true;
        kinfo("usb HID diag: %x:%x interface=%u Input ID=%u bytes=%zu expected=%zu action=%s\n",
            binding->vendor, binding->product, interface->number, id, bytes, expected,
            bytes > expected ? "ignore-trailer" : "zero-extend");
        break;
      }
    }
  }
  return usb_hid_mouse_decode(&binding->mouse, binding->control, bytes, sample);
}

static void diagnose_mouse_sample(struct usb_hid_binding *binding,
    struct usb_hid_interface *interface, size_t bytes)
{
  if (!diagnostics_enabled() || interface->protocol != USB_PROTOCOL_MOUSE || !interface->report_protocol ||
      interface->mouse_samples == USB_HID_MOUSE_SAMPLES) {
    return;
  }
  ++interface->mouse_samples;
  size_t captured = binding->mouse.keyboard_data ? 0 :
    bytes < USB_HID_MOUSE_SAMPLE_BYTES ? bytes : USB_HID_MOUSE_SAMPLE_BYTES;
  char line[USB_HID_MOUSE_SAMPLE_BYTES * 3 + 1];
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < captured; ++i) {
    uint8_t byte = binding->control[i];
    line[i * 3] = digits[byte >> 4];
    line[i * 3 + 1] = digits[byte & 15];
    line[i * 3 + 2] = ' ';
  }
  line[captured * 3] = 0;
  kinfo("usb HID mouse sample: %x:%x interface=%u sample=%u ID=%u bytes=%zu captured=%zu "
      "keyboard-data=%u: %s\n",
      binding->vendor, binding->product, interface->number, interface->mouse_samples,
      bytes && interface->report_protocol && binding->mouse.ids ? binding->control[0] : 0,
      bytes, captured, binding->mouse.keyboard_data, line);
}

static bool malformed_report(struct usb_hid_binding *binding,
    struct usb_hid_interface *interface, size_t bytes, bool decoded)
{
  if (interface->malformed_reports != UINT64_MAX) {
    ++interface->malformed_reports;
  }
  bool retire = ++interface->malformed_consecutive == USB_HID_MALFORMED_LIMIT;
  if (retire) {
    usb_hid_lost(binding);
  }
  uint64_t now = arch_monotonic_ns();
  if (diagnostics_enabled() &&
      (retire || !interface->malformed_diagnostic_ns || now >= interface->malformed_diagnostic_ns)) {
    interface->malformed_diagnostic_ns = now + USB_HID_MALFORMED_DIAGNOSTIC_NS;
    diagnose_report(binding, interface, retire ? "malformed-retirement" : "dropped",
        USB_INVALID, bytes, decoded);
  }
  return retire;
}

void usb_hid_collect(struct usb_hid_binding *binding)
{
  if (!binding->active) {
    return;
  }
  if (!usb_host_device_present(binding->host)) {
    usb_hid_lost(binding);
    for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
      struct usb_hid_interface *interface = &binding->interfaces[index];
      if (interface->protocol && interface->stream) {
        diagnose_report(binding, interface, "unavailable", USB_IO, 0, false);
      }
    }
    return;
  }
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    struct usb_hid_interface *interface = &binding->interfaces[index];
    if (!interface->protocol || !interface->stream) {
      continue;
    }
    for (unsigned packet = 0; packet < USB_INTERRUPT_COMPLETIONS; ++packet) {
      struct usb_interrupt_completion completion;
      enum usb_result result = usb_host_interrupt_take(interface->stream, binding->control,
                                                      sizeof(binding->control), &completion);
      if (result == USB_BUSY) {
        break;
      }
      if (result != USB_OK) {
        usb_hid_lost(binding);
        diagnose_report(binding, interface, "transport-loss", result, 0, false);
        return;
      }
      bool keys[KEY_COUNT], unresolved;
      if (interface->protocol == USB_PROTOCOL_EDGE) {
        struct gamepad_state state;
        if (!usb_edge_decode(binding->control, completion.bytes, &state)) {
          if (malformed_report(binding, interface, completion.bytes, false)) {
            return;
          }
          continue;
        }
        interface->malformed_consecutive = 0;
        uint64_t flags = cpu_save_interrupts();
        gamepad_source_report(&binding->gamepad, &state);
        cpu_restore_interrupts(flags);
      } else if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
        if (!keyboard_snapshot(binding->control, completion.bytes, keys, &unresolved)) {
          if (malformed_report(binding, interface, completion.bytes, false)) {
            return;
          }
          continue;
        }
        interface->malformed_consecutive = 0;
        uint64_t flags = cpu_save_interrupts();
        if (unresolved) {
          input_keyboard_unresolved(&binding->source);
        } else {
          input_keyboard_snapshot(&binding->source, keys);
        }
        cpu_restore_interrupts(flags);
      } else {
        struct usb_hid_mouse_sample sample;
        diagnose_mouse_sample(binding, interface, completion.bytes);
        mouse_wire_protocol(binding, interface, completion.bytes);
        bool valid = interface->report_protocol ?
          decode_mouse_report(binding, interface, completion.bytes, &sample) :
          boot_mouse_sample(interface, binding->control, completion.bytes, &sample);
        if (!valid) {
          if (malformed_report(binding, interface, completion.bytes, true)) {
            return;
          }
          continue;
        }
        interface->malformed_consecutive = 0;
        uint64_t flags = cpu_save_interrupts();
        input_pointer_report(&binding->source, sample.x, sample.y,
                             sample.pan, sample.wheel, sample.buttons);
        cpu_restore_interrupts(flags);
      }
    }
  }
}
