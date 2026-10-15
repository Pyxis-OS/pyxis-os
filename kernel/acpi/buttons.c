/* SPDX-License-Identifier: MPL-2.0 */
#include <kernel/log.h>
#include <uacpi/acpi.h>
#include <uacpi/event.h>
#include <uacpi/kernel_api.h>
#include <uacpi/namespace.h>
#include <uacpi/notify.h>
#include <uacpi/tables.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

#define CONTROL_POWER_HID "PNP0C0C"
#define INTEL_POWER_HID "INTC10CC"
#define CONTROL_POWER_PRESS 0x80u
#define INTEL_POWER_PRESS 0xceu
#define INTEL_HID_EVENT 0xc0u
#define INTEL_HID_POWER_INDEX 9u
#define INTEL_DSM_REVISION 1u
#define INTEL_DSM_BUTTON_LOAD 1u
#define INTEL_DSM_MODE 2u
#define INTEL_DSM_EVENT_INDEX 4u
#define INTEL_DSM_BUTTON_ENABLE 6u
#define INTEL_DSM_EVENT_CAPS_V1 7u
#define INTEL_DSM_EVENT_CAPS_V2 9u
#define INTEL_ARRAY_V1_MASK 0x20000u
#define INTEL_ARRAY_V2_MASK 0x60000u

/* Intel HID event-filter UUID, in ACPI's mixed-endian byte order. */
static const uint8_t intel_hid_uuid[] = {
  0xb3, 0x56, 0xec, 0xee, 0x42, 0x44, 0x8f, 0x40,
  0xa7, 0x92, 0x4e, 0xdd, 0x4d, 0x75, 0x80, 0x54,
};

/* Namespace and registration contexts stay owned by the worker until reboot. */
struct power_button {
  struct power_button *next;
  uacpi_namespace_node *node;
  uint16_t dsm_functions;
  bool intel, enabled;
};
static struct power_button *buttons;

static uacpi_status intel_dsm(struct power_button *button, unsigned function,
    const uint64_t *argument, uacpi_object **result)
{
  uacpi_object *input = argument ? uacpi_object_create_integer(*argument) : UACPI_NULL;
  if (argument && !input) {
    return UACPI_STATUS_OUT_OF_MEMORY;
  }
  uacpi_object *objects[] = {
    uacpi_object_create_buffer((uacpi_data_view){
      .const_bytes = intel_hid_uuid, .length = sizeof(intel_hid_uuid),
    }),
    uacpi_object_create_integer(INTEL_DSM_REVISION),
    uacpi_object_create_integer(function),
    uacpi_object_create_package((uacpi_object_array){
      .objects = input ? &input : UACPI_NULL, .count = input ? 1 : 0,
    }),
  };
  uacpi_status status = UACPI_STATUS_OUT_OF_MEMORY;
  if (objects[0] && objects[1] && objects[2] && objects[3]) {
    status = uacpi_eval(button->node, "_DSM",
        &(uacpi_object_array){.objects = objects, .count = 4}, result);
  }
  for (size_t i = 0; i < 4; ++i) {
    if (objects[i]) {
      uacpi_object_unref(objects[i]);
    }
  }
  if (input) {
    uacpi_object_unref(input);
  }
  return status;
}

static bool intel_integer(struct power_button *button, unsigned function,
    const char *method, uint64_t *value)
{
  if (button->dsm_functions & (1u << function)) {
    uacpi_object *result = UACPI_NULL;
    bool success = intel_dsm(button, function, UACPI_NULL, &result) == UACPI_STATUS_OK && result &&
        uacpi_object_get_integer(result, value) == UACPI_STATUS_OK;
    if (result) {
      uacpi_object_unref(result);
    }
    if (success) {
      return true;
    }
  }
  return uacpi_eval_simple_integer(button->node, method, value) == UACPI_STATUS_OK;
}

static bool intel_execute(struct power_button *button, unsigned function,
    const char *method, const uint64_t *argument)
{
  /* A successful firmware call may return nothing or an arbitrary value.
   * Never repeat its side effects because the return value is not an integer. */
  if ((button->dsm_functions & (1u << function)) &&
      intel_dsm(button, function, argument, UACPI_NULL) == UACPI_STATUS_OK) {
    return true;
  }
  if (!argument) {
    return uacpi_execute_simple(button->node, method) == UACPI_STATUS_OK;
  }
  uacpi_object *input = uacpi_object_create_integer(*argument);
  if (!input) {
    return false;
  }
  uacpi_status status = uacpi_execute(button->node, method,
      &(uacpi_object_array){.objects = &input, .count = 1});
  uacpi_object_unref(input);
  return status == UACPI_STATUS_OK;
}

static void enable_intel_array(struct power_button *button)
{
  uint64_t events;
  bool present = intel_integer(button, INTEL_DSM_EVENT_CAPS_V2, "HEBC", &events) &&
      (events & INTEL_ARRAY_V2_MASK);
  if (!present) {
    present = intel_integer(button, INTEL_DSM_EVENT_CAPS_V1, "HEBC", &events) &&
        (events & INTEL_ARRAY_V1_MASK);
  }
  if (!present) {
    return;
  }
  uint64_t capabilities;
  if (uacpi_eval_simple_integer(button->node, "BTNC", &capabilities) != UACPI_STATUS_OK ||
      !intel_execute(button, INTEL_DSM_BUTTON_ENABLE, "BTNE", &capabilities)) {
    kinfo("ACPI: Intel HID button array enable unavailable; power Notify handler retained\n");
  } else {
    kinfo("ACPI: Intel HID button array enable requested, capabilities=0x%llx; power events only\n",
        (unsigned long long)capabilities);
  }
}

static bool prepare_intel(struct power_button *button)
{
  uacpi_object *result = UACPI_NULL;
  uacpi_data_view mask;
  if (intel_dsm(button, 0, UACPI_NULL, &result) == UACPI_STATUS_OK && result &&
      uacpi_object_get_buffer(result, &mask) == UACPI_STATUS_OK && mask.length &&
      (mask.const_bytes[0] & 1)) {
    button->dsm_functions = mask.const_bytes[0];
    if (mask.length > 1) {
      button->dsm_functions |= (uint16_t)mask.const_bytes[1] << 8;
    }
  }
  if (result) {
    uacpi_object_unref(result);
  }
  uint64_t mode;
  if (!intel_integer(button, INTEL_DSM_MODE, "HDMM", &mode) || mode != 0) {
    return false;
  }
  enable_intel_array(button);
  /* BTNL activates power events and applies the firmware's final array policy. */
  if (!intel_execute(button, INTEL_DSM_BUTTON_LOAD, "BTNL", UACPI_NULL)) {
    return false;
  }
  return true;
}

static uacpi_status button_notify(uacpi_handle context, uacpi_namespace_node *node,
    uacpi_u64 value)
{
  acpi_require_worker();
  struct power_button *button = context;
  if (!button->enabled || node != button->node) {
    return UACPI_STATUS_OK;
  }
  bool pressed = value == (button->intel ? INTEL_POWER_PRESS : CONTROL_POWER_PRESS);
  if (button->intel && value == INTEL_HID_EVENT) {
    uint64_t index;
    if (!intel_integer(button, INTEL_DSM_EVENT_INDEX, "HDEM", &index)) {
      ktrace("ACPI: Intel HID power event index unavailable\n");
      return UACPI_STATUS_OK;
    }
    pressed = index == INTEL_HID_POWER_INDEX;
  }
  if (pressed) {
    acpi_button_press();
  }
  return UACPI_STATUS_OK;
}

static void report_route(struct power_button *button)
{
  uacpi_object *prw = UACPI_NULL;
  uacpi_object_array elements;
  uint64_t candidate;
  uacpi_status status = uacpi_eval_simple_package(button->node, "_PRW", &prw);
  bool root_gpe = status == UACPI_STATUS_OK && prw &&
      uacpi_object_get_package(prw, &elements) == UACPI_STATUS_OK && elements.count >= 2 &&
      uacpi_object_get_integer(elements.objects[0], &candidate) == UACPI_STATUS_OK &&
      candidate <= UINT16_MAX;
  const char *path = uacpi_namespace_node_generate_absolute_path(button->node);
  const char *hid = button->intel ? INTEL_POWER_HID : CONTROL_POWER_HID;
  /* _PRW alone cannot prove an additional runtime route. Existing EC queries
   * can already deliver these notifications without another GPE enable. */
  if (root_gpe) {
    kinfo("ACPI: power button %s %s: Notify ready; additional GPE unqualified (_PRW candidate %u), none enabled\n",
        hid, path ? path : "?", (unsigned)candidate);
  } else {
    kinfo("ACPI: power button %s %s: Notify ready; additional GPE unqualified (_PRW %s), none enabled\n",
        hid, path ? path : "?", status == UACPI_STATUS_OK ? "no usable integer candidate" : "unavailable");
  }
  if (path) {
    uacpi_free_absolute_path(path);
  }
  if (prw) {
    uacpi_object_unref(prw);
  }
}

static uacpi_iteration_decision add_button(void *context, uacpi_namespace_node *node,
    uacpi_u32 depth)
{
  (void)depth;
  bool intel = context != UACPI_NULL;
  for (struct power_button *button = buttons; button; button = button->next) {
    if (button->node == node) {
      return UACPI_ITERATION_DECISION_CONTINUE;
    }
  }
  struct power_button *button = uacpi_kernel_alloc(sizeof(*button));
  uacpi_status status = UACPI_STATUS_OUT_OF_MEMORY;
  if (button) {
    *button = (struct power_button){.node = node, .intel = intel};
    status = uacpi_install_notify_handler(node, button_notify, button);
  }
  if (status != UACPI_STATUS_OK) {
    kinfo("ACPI: power button %s: Notify registration failed: %s\n",
        intel ? INTEL_POWER_HID : CONTROL_POWER_HID, uacpi_status_to_string(status));
    if (button) {
      uacpi_kernel_free(button, sizeof(*button));
    }
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  button->next = buttons;
  buttons = button;
  if (intel && !prepare_intel(button)) {
    kinfo("ACPI: power button %s: simple mode/BTNL setup unavailable; Notify input disabled\n",
        INTEL_POWER_HID);
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  button->enabled = true;
  report_route(button);
  return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_interrupt_ret fixed_power_button(uacpi_handle context)
{
  (void)context;
  acpi_button_press();
  return UACPI_INTERRUPT_HANDLED;
}

void acpi_buttons_start(void)
{
  acpi_require_worker();
  struct acpi_fadt *fadt;
  if (uacpi_table_fadt(&fadt) == UACPI_STATUS_OK &&
      !(fadt->flags & (ACPI_PWR_BUTTON | ACPI_HW_REDUCED_ACPI))) {
    uacpi_status status = uacpi_clear_fixed_event(UACPI_FIXED_EVENT_POWER_BUTTON);
    if (status == UACPI_STATUS_OK) {
      status = uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON,
          fixed_power_button, UACPI_NULL);
    }
    if (status != UACPI_STATUS_OK) {
      kinfo("ACPI: error: power button not enabled: %s\n", uacpi_status_to_string(status));
    }
  }
  uacpi_status status = uacpi_find_devices(CONTROL_POWER_HID, add_button, UACPI_NULL);
  if (status == UACPI_STATUS_OK) {
    status = uacpi_find_devices(INTEL_POWER_HID, add_button, (void *)1);
  }
  if (status != UACPI_STATUS_OK) {
    kinfo("ACPI: power button discovery failed: %s\n", uacpi_status_to_string(status));
  }
}
