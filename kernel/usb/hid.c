#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include "hid.h"

#define USB_CONFIGURATION_BYTES 9
#define USB_DESCRIPTOR_INTERFACE 4
#define USB_DESCRIPTOR_ENDPOINT 5
#define USB_DESCRIPTOR_HID 0x21
#define USB_DESCRIPTOR_REPORT 0x22
#define USB_CLASS_HID 3
#define USB_SUBCLASS_BOOT 1
#define USB_PROTOCOL_KEYBOARD 1
#define USB_PROTOCOL_MOUSE 2
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_INTERRUPT 3
#define USB_ENDPOINT_TYPE_MASK 3
#define USB_PACKET_MASK 0x07ff
#define USB_PACKET_TRANSACTIONS_SHIFT 11
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_GET_REPORT 1
#define USB_REQUEST_SET_IDLE 10
#define USB_REQUEST_SET_PROTOCOL 11
#define USB_REQUEST_INTERFACE_IN 0x81
#define USB_REQUEST_CLASS_INTERFACE_IN 0xa1
#define USB_REQUEST_CLASS_INTERFACE_OUT 0x21
#define USB_REPORT_INPUT 1
#define USB_MODIFIER_FIRST 0xe0
#define USB_KEY_ERROR_LAST 3
#define USB_QEMU_VENDOR 0x0627
#define USB_QEMU_PRODUCT 0x0001
#define USB_QEMU_MOUSE_PACKET 4
#define USB_QEMU_MOUSE_DESCRIPTOR_BYTES 52

enum hid_stage {
  HID_ENDPOINTS, HID_CONFIGURATION, HID_WHEEL_DESCRIPTOR, HID_PROTOCOL,
  HID_IDLE, HID_INITIAL, HID_START, HID_FINISHED,
};

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
  unsigned index = interface->protocol == USB_PROTOCOL_KEYBOARD ? 0 : 1;
  if (!candidate->interfaces[index].protocol) {
    candidate->interfaces[index] = *interface;
  }
}

void usb_hid_select(struct usb_hid_binding *binding, const uint8_t *configuration,
                    size_t bytes, enum usb_speed speed, uint16_t vendor, uint16_t product)
{
  if (binding->configuration || (speed != USB_SPEED_LOW && speed != USB_SPEED_FULL &&
      speed != USB_SPEED_HIGH)) {
    return;
  }
  struct usb_hid_binding candidate = {.configuration = configuration[5]};
  struct usb_hid_interface interface = {0};
  unsigned inputs = 0;
  for (size_t offset = USB_CONFIGURATION_BYTES; offset < bytes; offset += configuration[offset]) {
    const uint8_t *part = configuration + offset;
    if (part[1] == USB_DESCRIPTOR_INTERFACE) {
      select_interface(&candidate, &interface, inputs);
      interface = (struct usb_hid_interface){0};
      inputs = 0;
      if (!part[3] && part[5] == USB_CLASS_HID && part[6] == USB_SUBCLASS_BOOT &&
          (part[7] == USB_PROTOCOL_KEYBOARD || part[7] == USB_PROTOCOL_MOUSE)) {
        interface.number = part[2];
        interface.protocol = part[7];
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
      interface.receive_bytes = interface.endpoint.packet;
      unsigned minimum = interface.protocol == USB_PROTOCOL_KEYBOARD ?
        USB_HID_KEYBOARD_BYTES : USB_HID_MOUSE_BYTES;
      if (interface.receive_bytes < minimum) {
        interface.receive_bytes = minimum;
      }
    }
  }
  select_interface(&candidate, &interface, inputs);
  if (!candidate.interfaces[0].protocol && !candidate.interfaces[1].protocol) {
    return;
  }
  struct usb_hid_interface *mouse = &candidate.interfaces[1];
  mouse->qemu_wheel_candidate = mouse->protocol && vendor == USB_QEMU_VENDOR &&
    product == USB_QEMU_PRODUCT && candidate.configuration == 1 && !mouse->number &&
    mouse->endpoint.address == 0x81 && mouse->endpoint.packet == USB_QEMU_MOUSE_PACKET &&
    !mouse->endpoint.transactions && mouse->descriptor_bytes == USB_QEMU_MOUSE_DESCRIPTOR_BYTES;
  *binding = candidate;
}

static bool keyboard_snapshot(const uint8_t *report, size_t bytes, bool keys[KEY_COUNT],
                               bool *unresolved)
{
  *unresolved = false;
  if (bytes != USB_HID_KEYBOARD_BYTES || report[1]) {
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
    input_source_lost(&binding->source);
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
  ktrace("usb HID: source detached\n");
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
    if (binding->interfaces[index].receive_bytes > usb_host_interrupt_capacity()) {
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
  binding->interface = 0;
}

static struct usb_setup bind_request(const struct usb_hid_binding *binding)
{
  const struct usb_hid_interface *interface = &binding->interfaces[binding->interface];
  struct usb_setup setup = {
    .request_type = USB_REQUEST_CLASS_INTERFACE_OUT,
    .index = interface->number,
  };
  switch (binding->stage) {
  case HID_WHEEL_DESCRIPTOR:
    setup.request_type = USB_REQUEST_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_DESCRIPTOR;
    setup.value = USB_DESCRIPTOR_REPORT << 8;
    setup.length = USB_QEMU_MOUSE_DESCRIPTOR_BYTES;
    break;
  case HID_CONFIGURATION:
    setup = (struct usb_setup){.request = USB_REQUEST_SET_CONFIGURATION,
                              .value = binding->configuration};
    break;
  case HID_PROTOCOL:
    setup.request = USB_REQUEST_SET_PROTOCOL;
    break;
  case HID_IDLE:
    setup.request = USB_REQUEST_SET_IDLE;
    break;
  case HID_INITIAL:
    setup.request_type = USB_REQUEST_CLASS_INTERFACE_IN;
    setup.request = USB_REQUEST_GET_REPORT;
    setup.value = USB_REPORT_INPUT << 8;
    setup.length = interface->receive_bytes;
    break;
  default:
    break;
  }
  return setup;
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
  if (binding->stage == HID_START) {
    uint64_t flags = cpu_save_interrupts();
    input_source_attach(&binding->source, binding->interfaces[0].protocol != 0,
                        binding->interfaces[1].protocol != 0);
    if (binding->interfaces[0].protocol) {
      input_source_initial_keyboard(&binding->source, binding->initial_keys);
    }
    if (binding->interfaces[1].protocol) {
      input_source_initial_pointer(&binding->source, binding->initial_buttons);
    }
    binding->active = true;
    cpu_restore_interrupts(flags);
    for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
      if (binding->interfaces[index].stream) {
        enum usb_result result = usb_host_interrupt_start(binding->interfaces[index].stream);
        if (result != USB_OK) {
          return bind_failed(binding, result);
        }
      }
    }
    binding->result = USB_OK;
    binding->stage = HID_FINISHED;
    ktrace("usb HID: boot %s%s source attached\n",
           binding->interfaces[0].protocol ? "keyboard" : "",
           binding->interfaces[1].protocol ? "/mouse" : "");
    return USB_OK;
  }
  if (binding->stage != HID_CONFIGURATION) {
    while (binding->interface < USB_HID_ENDPOINTS_PER_DEVICE &&
           (!binding->interfaces[binding->interface].protocol ||
            (binding->stage == HID_WHEEL_DESCRIPTOR &&
             !binding->interfaces[binding->interface].qemu_wheel_candidate))) {
      ++binding->interface;
    }
    if (binding->interface == USB_HID_ENDPOINTS_PER_DEVICE) {
      next_stage(binding);
      return USB_BUSY;
    }
  }
  struct usb_setup setup = bind_request(binding);
  if (!binding->ticket_active) {
    uint64_t deadline = task_deadline_after_ms(USB_CONTROL_TIMEOUT_MS);
    if (deadline > binding->deadline) {
      deadline = binding->deadline;
    }
    enum usb_result result = usb_host_control_submit(binding->host, &setup, NULL,
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
  uint8_t report[USB_INTERRUPT_BYTES];
  struct usb_completion completion;
  result = usb_host_control_take(binding->host, binding->ticket, report, sizeof(report), &completion);
  if (result != USB_OK) {
    return bind_failed(binding, result);
  }
  binding->ticket_active = false;
  struct usb_hid_interface *interface = &binding->interfaces[binding->interface];
  bool optional_stall = completion.result == USB_STALL &&
      ((binding->stage == HID_IDLE && interface->protocol == USB_PROTOCOL_MOUSE) ||
       binding->stage == HID_WHEEL_DESCRIPTOR);
  if ((completion.result != USB_OK && !optional_stall) ||
      (!setup.length && completion.bytes)) {
    return bind_failed(binding, completion.result == USB_OK ? USB_IO : completion.result);
  }
  if (binding->stage == HID_WHEEL_DESCRIPTOR) {
    interface->qemu_wheel = completion.result == USB_OK &&
      completion.bytes == sizeof(qemu_mouse_descriptor) &&
      !memcmp(report, qemu_mouse_descriptor, sizeof(qemu_mouse_descriptor));
  } else if (binding->stage == HID_INITIAL) {
    if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
      bool unresolved;
      if (!keyboard_snapshot(report, completion.bytes, binding->initial_keys, &unresolved) || unresolved) {
        return bind_failed(binding, USB_IO);
      }
    } else {
      if (completion.bytes < USB_HID_MOUSE_BYTES ||
          (interface->qemu_wheel && completion.bytes != USB_QEMU_MOUSE_PACKET)) {
        return bind_failed(binding, USB_IO);
      }
      binding->initial_buttons = report[0] & 7;
    }
  }
  if (binding->stage == HID_CONFIGURATION) {
    next_stage(binding);
  } else {
    ++binding->interface;
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

void usb_hid_collect(struct usb_hid_binding *binding)
{
  if (!binding->active) {
    return;
  }
  if (!usb_host_device_present(binding->host)) {
    usb_hid_lost(binding);
    return;
  }
  for (unsigned index = 0; index < USB_HID_ENDPOINTS_PER_DEVICE; ++index) {
    struct usb_hid_interface *interface = &binding->interfaces[index];
    if (!interface->stream) {
      continue;
    }
    for (unsigned packet = 0; packet < USB_INTERRUPT_COMPLETIONS; ++packet) {
      uint8_t report[USB_INTERRUPT_BYTES];
      struct usb_interrupt_completion completion;
      enum usb_result result = usb_host_interrupt_take(interface->stream, report,
                                                      sizeof(report), &completion);
      if (result == USB_BUSY) {
        break;
      }
      if (result != USB_OK) {
        usb_hid_lost(binding);
        return;
      }
      bool keys[KEY_COUNT], unresolved;
      if (interface->protocol == USB_PROTOCOL_KEYBOARD) {
        if (!keyboard_snapshot(report, completion.bytes, keys, &unresolved)) {
          usb_hid_lost(binding);
          return;
        }
        uint64_t flags = cpu_save_interrupts();
        if (unresolved) {
          input_keyboard_unresolved(&binding->source);
        } else {
          input_keyboard_snapshot(&binding->source, keys);
        }
        cpu_restore_interrupts(flags);
      } else {
        if (completion.bytes < USB_HID_MOUSE_BYTES ||
            (interface->qemu_wheel && completion.bytes != USB_QEMU_MOUSE_PACKET)) {
          usb_hid_lost(binding);
          return;
        }
        int32_t wheel = interface->qemu_wheel ? -(int8_t)report[3] : 0;
        uint64_t flags = cpu_save_interrupts();
        input_pointer_report(&binding->source, (int8_t)report[1], (int8_t)report[2],
                             wheel, report[0] & 7);
        cpu_restore_interrupts(flags);
      }
    }
  }
}
