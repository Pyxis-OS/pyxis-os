#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <uacpi/acpi.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

/* Control-method batteries (PNP0C0A) and AC adapters (ACPI0003), polled from
 * the ACPI worker. Percentages follow Linux's capacity: remaining capacity
 * over last full charge capacity, rounded down. */

#define BATTERY_POLL_NS UINT64_C(5000000000)
#define BATTERY_LIMIT 2
#define BATTERY_HID "PNP0C0A"
#define AC_ADAPTER_HID "ACPI0003"
/* _BIF, _BIX and _BST report an unknown value as all ones, the ABI's UNKNOWN. */
#define BATTERY_UNKNOWN UINT64_C(0xffffffff)
#define POWER_UNIT_MAH 1

/* Package indices (ACPI 6.5, sections 10.2.2.1, 10.2.2.2 and 10.2.2.11). */
struct info_layout {
  size_t unit, design_capacity, last_full_capacity, cycle_count;
  size_t model, serial, type, oem;
  bool has_cycle_count;
};

static const struct info_layout bix_layout = {
  .unit = 1, .design_capacity = 2, .last_full_capacity = 3, .cycle_count = 8,
  .model = 16, .serial = 17, .type = 18, .oem = 19, .has_cycle_count = true,
};

static const struct info_layout bif_layout = {
  .unit = 0, .design_capacity = 1, .last_full_capacity = 2,
  .model = 9, .serial = 10, .type = 11, .oem = 12,
};

#define BST_STATE 0
#define BST_PRESENT_RATE 1
#define BST_REMAINING_CAPACITY 2
#define BST_VOLTAGE 3
#define BST_STATE_DISCHARGING (1u << 0)
#define BST_STATE_CHARGING (1u << 1)
#define BST_STATE_CRITICAL (1u << 2)

/* Worker only. The information package is read when a battery appears, and
 * again on each poll until its full capacity is known; info keeps its fields. */
static struct battery {
  uacpi_namespace_node *node;
  bool present;
  uint64_t full;
  struct system_info_battery info;
} batteries[BATTERY_LIMIT];
static size_t battery_count;
static uacpi_namespace_node *ac_adapter;
static uint64_t next_poll;

/* Written by the worker; read with IF=0 on the BSP by the presenter and the
 * system_info executor. */
static struct {
  struct acpi_battery_status summary;
  struct system_info_power power;
  struct system_info_battery batteries[BATTERY_LIMIT];
} published;

static uacpi_iteration_decision add_battery(void *user, uacpi_namespace_node *node,
                                            uacpi_u32 depth)
{
  (void)user;
  (void)depth;
  if (battery_count == BATTERY_LIMIT) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  batteries[battery_count++] = (struct battery){.node = node};
  return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision add_ac_adapter(void *user, uacpi_namespace_node *node,
                                               uacpi_u32 depth)
{
  (void)user;
  (void)depth;
  ac_adapter = node;
  return UACPI_ITERATION_DECISION_BREAK;
}

static uacpi_object *package_element(uacpi_object *package, size_t index)
{
  uacpi_object_array elements;
  if (uacpi_object_get_package(package, &elements) != UACPI_STATUS_OK ||
      index >= elements.count) {
    return UACPI_NULL;
  }
  return elements.objects[index];
}

static bool package_integer(uacpi_object *package, size_t index, uint64_t *value)
{
  uacpi_object *element = package_element(package, index);
  return element && uacpi_object_get_integer(element, value) == UACPI_STATUS_OK;
}

/* A missing element or a value beyond 32 bits reads as unknown. */
static uint32_t package_u32(uacpi_object *package, size_t index)
{
  uint64_t value;
  if (!package_integer(package, index, &value) || value > BATTERY_UNKNOWN) {
    return SYSTEM_INFO_BATTERY_UNKNOWN;
  }
  return (uint32_t)value;
}

/* Firmware strings may be strings or buffers; either is copied up to its first
 * NUL and truncated to leave room for one. */
static void package_string(uacpi_object *package, size_t index, char *out, size_t size)
{
  uacpi_object *element = package_element(package, index);
  uacpi_data_view view;
  if (!element || uacpi_object_get_string_or_buffer(element, &view) != UACPI_STATUS_OK) {
    return;
  }
  for (size_t i = 0; i < view.length && i + 1 < size && view.const_text[i]; ++i) {
    out[i] = view.const_text[i];
  }
}

static bool known(uint32_t capacity)
{
  return capacity && capacity != SYSTEM_INFO_BATTERY_UNKNOWN;
}

/* _BIX where the firmware has it, otherwise _BIF. A battery that has not
 * learned its full capacity yet is measured against its design capacity. */
static void read_info(struct battery *battery)
{
  const struct info_layout *layout = &bix_layout;
  uacpi_object *package;
  if (uacpi_eval_simple_package(battery->node, "_BIX", &package) != UACPI_STATUS_OK) {
    layout = &bif_layout;
    if (uacpi_eval_simple_package(battery->node, "_BIF", &package) != UACPI_STATUS_OK) {
      battery->full = 0;
      return;
    }
  }

  struct system_info_battery *info = &battery->info;
  *info = (struct system_info_battery){
    .unit = package_u32(package, layout->unit),
    .design_capacity = package_u32(package, layout->design_capacity),
    .last_full_capacity = package_u32(package, layout->last_full_capacity),
    .cycle_count = layout->has_cycle_count ? package_u32(package, layout->cycle_count) :
                                             SYSTEM_INFO_BATTERY_UNKNOWN,
  };
  if (info->unit != SYSTEM_INFO_BATTERY_UNKNOWN) {
    info->unit = info->unit == POWER_UNIT_MAH ? SYSTEM_INFO_BATTERY_UNIT_MAH :
                                                SYSTEM_INFO_BATTERY_UNIT_MWH;
  }
  package_string(package, layout->model, info->model, sizeof(info->model));
  package_string(package, layout->serial, info->serial, sizeof(info->serial));
  package_string(package, layout->type, info->type, sizeof(info->type));
  package_string(package, layout->oem, info->oem, sizeof(info->oem));
  uacpi_object_unref(package);

  battery->full = known(info->last_full_capacity) ? info->last_full_capacity :
                  known(info->design_capacity)    ? info->design_capacity : 0;
}

static bool battery_present(uacpi_namespace_node *node)
{
  uacpi_u32 flags;
  return uacpi_eval_sta(node, &flags) == UACPI_STATUS_OK &&
         (flags & ACPI_STA_RESULT_DEVICE_BATTERY_PRESENT);
}

static const struct system_info_battery absent = {
  .percent = SYSTEM_INFO_BATTERY_UNKNOWN,
  .unit = SYSTEM_INFO_BATTERY_UNKNOWN,
  .remaining_capacity = SYSTEM_INFO_BATTERY_UNKNOWN,
  .last_full_capacity = SYSTEM_INFO_BATTERY_UNKNOWN,
  .design_capacity = SYSTEM_INFO_BATTERY_UNKNOWN,
  .present_rate = SYSTEM_INFO_BATTERY_UNKNOWN,
  .voltage = SYSTEM_INFO_BATTERY_UNKNOWN,
  .cycle_count = SYSTEM_INFO_BATTERY_UNKNOWN,
};

/* Fills RECORD from the battery's information and a fresh _BST. Returns
 * whether the remaining capacity is known against a known full capacity. */
static bool read_battery(struct battery *battery, struct system_info_battery *record)
{
  bool present = battery_present(battery->node);
  if (present && (!battery->present || !battery->full)) {
    read_info(battery);
  }
  battery->present = present;
  if (!present) {
    *record = absent;
    return false;
  }

  *record = battery->info;
  record->flags = SYSTEM_INFO_BATTERY_PRESENT;
  record->percent = SYSTEM_INFO_BATTERY_UNKNOWN;
  record->remaining_capacity = SYSTEM_INFO_BATTERY_UNKNOWN;
  record->present_rate = SYSTEM_INFO_BATTERY_UNKNOWN;
  record->voltage = SYSTEM_INFO_BATTERY_UNKNOWN;
  uacpi_object *state;
  if (uacpi_eval_simple_package(battery->node, "_BST", &state) != UACPI_STATUS_OK) {
    return false;
  }
  uint64_t flags = 0;
  package_integer(state, BST_STATE, &flags);
  record->present_rate = package_u32(state, BST_PRESENT_RATE);
  record->remaining_capacity = package_u32(state, BST_REMAINING_CAPACITY);
  record->voltage = package_u32(state, BST_VOLTAGE);
  uacpi_object_unref(state);

  if (flags & BST_STATE_DISCHARGING) {
    record->flags |= SYSTEM_INFO_BATTERY_DISCHARGING;
  }
  if (flags & BST_STATE_CHARGING) {
    record->flags |= SYSTEM_INFO_BATTERY_CHARGING;
  }
  if (flags & BST_STATE_CRITICAL) {
    record->flags |= SYSTEM_INFO_BATTERY_CRITICAL;
  }
  if (!battery->full || record->remaining_capacity == SYSTEM_INFO_BATTERY_UNKNOWN) {
    return false;
  }
  uint64_t percent = (uint64_t)record->remaining_capacity * 100 / battery->full;
  record->percent = (uint32_t)(percent > 100 ? 100 : percent);
  return true;
}

static uint32_t read_ac(void)
{
  uint64_t online;
  if (!ac_adapter || uacpi_eval_simple_integer(ac_adapter, "_PSR", &online) != UACPI_STATUS_OK) {
    return SYSTEM_INFO_AC_UNKNOWN;
  }
  return online ? SYSTEM_INFO_AC_ONLINE : SYSTEM_INFO_AC_OFFLINE;
}

static void poll(uint64_t now)
{
  struct system_info_battery records[BATTERY_LIMIT];
  uint64_t remaining = 0, full = 0;
  bool any = false, charging = false;
  for (size_t i = 0; i < battery_count; ++i) {
    if (read_battery(&batteries[i], &records[i])) {
      remaining += records[i].remaining_capacity;
      full += batteries[i].full;
      any = true;
    }
    records[i].sample_ns = now;
    charging |= (records[i].flags & SYSTEM_INFO_BATTERY_CHARGING) != 0;
  }

  struct system_info_power power = {
    .sample_ns = now, .ac = read_ac(), .battery_count = (uint32_t)battery_count,
  };
  struct acpi_battery_status summary = {.ac_online = power.ac == SYSTEM_INFO_AC_ONLINE};
  if (any) {
    uint64_t percent = remaining * 100 / full;
    summary.present = true;
    summary.percent = (uint8_t)(percent > 100 ? 100 : percent);
    summary.charging = charging;
  }

  uint64_t flags = cpu_save_interrupts();
  published.summary = summary;
  published.power = power;
  for (size_t i = 0; i < battery_count; ++i) {
    published.batteries[i] = records[i];
  }
  cpu_restore_interrupts(flags);
}

void acpi_battery_start(void)
{
  uacpi_find_devices(BATTERY_HID, add_battery, UACPI_NULL);
  uacpi_find_devices(AC_ADAPTER_HID, add_ac_adapter, UACPI_NULL);
  ktrace("ACPI: batteries: %zu; AC adapter: %s\n", battery_count, ac_adapter ? "yes" : "no");
  next_poll = battery_count || ac_adapter ? arch_monotonic_ns() : UINT64_MAX;
}

uint64_t acpi_battery_poll(uint64_t now)
{
  if (now >= next_poll) {
    poll(now);
    next_poll = now + BATTERY_POLL_NS;
  }
  return next_poll;
}

static void require_bsp(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
}

struct acpi_battery_status acpi_battery_status(void)
{
  require_bsp();
  return published.summary;
}

struct system_info_power acpi_power_state(void)
{
  require_bsp();
  return published.power;
}

bool acpi_battery_read(uint64_t index, struct system_info_battery *battery)
{
  require_bsp();
  if (index >= published.power.battery_count) {
    return false;
  }
  *battery = published.batteries[index];
  return true;
}
