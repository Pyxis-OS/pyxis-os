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
/* _BIF, _BIX and _BST report an unknown value as all ones. */
#define BATTERY_UNKNOWN UINT64_C(0xffffffff)

/* Package indices (ACPI 6.5, sections 10.2.2.1, 10.2.2.2 and 10.2.2.11). */
#define BIF_DESIGN_CAPACITY 1
#define BIF_LAST_FULL_CAPACITY 2
#define BIX_DESIGN_CAPACITY 2
#define BIX_LAST_FULL_CAPACITY 3
#define BST_STATE 0
#define BST_REMAINING_CAPACITY 2
#define BST_STATE_CHARGING (1u << 1)

/* Worker only. A battery's full capacity is read when it appears, and again
 * on each poll until it is known. */
static struct battery {
  uacpi_namespace_node *node;
  bool present;
  uint64_t full;
} batteries[BATTERY_LIMIT];
static size_t battery_count;
static uacpi_namespace_node *ac_adapter;
static uint64_t next_poll;

/* Written by the worker and read by the presenter, both on the BSP with IF=0. */
static struct acpi_battery_status published;

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

static bool package_integer(uacpi_object *package, size_t index, uint64_t *value)
{
  uacpi_object_array elements;
  if (uacpi_object_get_package(package, &elements) != UACPI_STATUS_OK ||
      index >= elements.count) {
    return false;
  }
  return uacpi_object_get_integer(elements.objects[index], value) == UACPI_STATUS_OK;
}

static bool known(uint64_t value)
{
  return value && value != BATTERY_UNKNOWN;
}

/* _BIX where the firmware has it, otherwise _BIF. A battery that has not
 * learned its full capacity yet is measured against its design capacity. */
static uint64_t read_full_capacity(uacpi_namespace_node *node)
{
  uacpi_object *info;
  size_t design = BIX_DESIGN_CAPACITY, last_full = BIX_LAST_FULL_CAPACITY;
  if (uacpi_eval_simple_package(node, "_BIX", &info) != UACPI_STATUS_OK) {
    design = BIF_DESIGN_CAPACITY;
    last_full = BIF_LAST_FULL_CAPACITY;
    if (uacpi_eval_simple_package(node, "_BIF", &info) != UACPI_STATUS_OK) {
      return 0;
    }
  }

  uint64_t capacity = 0;
  if (!package_integer(info, last_full, &capacity) || !known(capacity)) {
    if (!package_integer(info, design, &capacity) || !known(capacity)) {
      capacity = 0;
    }
  }
  uacpi_object_unref(info);
  return capacity;
}

static bool battery_present(uacpi_namespace_node *node)
{
  uacpi_u32 flags;
  return uacpi_eval_sta(node, &flags) == UACPI_STATUS_OK &&
         (flags & ACPI_STA_RESULT_DEVICE_BATTERY_PRESENT);
}

struct reading {
  uint64_t remaining, full;
  bool charging;
};

static bool read_battery(struct battery *battery, struct reading *reading)
{
  bool present = battery_present(battery->node);
  if (present && (!battery->present || !battery->full)) {
    battery->full = read_full_capacity(battery->node);
  }
  battery->present = present;
  if (!present || !battery->full) {
    return false;
  }

  uacpi_object *state;
  if (uacpi_eval_simple_package(battery->node, "_BST", &state) != UACPI_STATUS_OK) {
    return false;
  }
  uint64_t flags, remaining;
  bool valid = package_integer(state, BST_STATE, &flags) &&
               package_integer(state, BST_REMAINING_CAPACITY, &remaining) &&
               remaining != BATTERY_UNKNOWN;
  uacpi_object_unref(state);
  if (!valid) {
    return false;
  }

  reading->remaining += remaining;
  reading->full += battery->full;
  reading->charging |= (flags & BST_STATE_CHARGING) != 0;
  return true;
}

static void poll(void)
{
  struct reading reading = {0};
  bool any = false;
  for (size_t i = 0; i < battery_count; ++i) {
    any |= read_battery(&batteries[i], &reading);
  }

  uint64_t online = 0;
  bool ac_online = ac_adapter &&
                   uacpi_eval_simple_integer(ac_adapter, "_PSR", &online) == UACPI_STATUS_OK &&
                   online;

  struct acpi_battery_status status = {.ac_online = ac_online};
  if (any) {
    uint64_t percent = reading.remaining * 100 / reading.full;
    status.present = true;
    status.percent = (uint8_t)(percent > 100 ? 100 : percent);
    status.charging = reading.charging;
  }

  uint64_t flags = cpu_save_interrupts();
  published = status;
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
    poll();
    next_poll = now + BATTERY_POLL_NS;
  }
  return next_poll;
}

struct acpi_battery_status acpi_battery_status(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return published;
}
