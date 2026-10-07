#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <uacpi/acpi.h>
#include <uacpi/event.h>
#include <uacpi/kernel_api.h>
#include <uacpi/namespace.h>
#include <uacpi/opregion.h>
#include <uacpi/resources.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

/* ACPI embedded controller (ACPI 6.5, section 12): two I/O ports, a status and
 * command register and a data register. Transactions poll the status
 * register; the EC's GPE is used only for its query events. */

#define EC_STATUS_OBF (1u << 0)
#define EC_STATUS_IBF (1u << 1)
#define EC_STATUS_SCI_EVT (1u << 5)

#define EC_COMMAND_READ 0x80
#define EC_COMMAND_WRITE 0x81
#define EC_COMMAND_QUERY 0x84

#define EC_ADDRESS_LIMIT 0x100
/* Each wait for the EC to take or produce a byte; Linux uses the same bound. */
#define EC_WAIT_NS UINT64_C(500000000)
/* Bounds one query work item, so a stuck SCI_EVT cannot keep the worker. */
#define EC_QUERIES_PER_EVENT 32
#define EC_HID "PNP0C09"
#define IO_PORT_LIMIT 0x10000

/* Worker only. */
static struct embedded_controller {
  uacpi_namespace_node *node;
  uint16_t data_port, command_port;
  uint16_t gpe;
  bool from_ecdt;
  bool handler_installed;
  bool query_queued;
  size_t timeouts;
} ec;

static bool wait_status(uint8_t mask, uint8_t wanted)
{
  uint64_t end = arch_monotonic_ns() + EC_WAIT_NS;
  for (;;) {
    if ((inb(ec.command_port) & mask) == wanted) {
      return true;
    }
    if (arch_monotonic_ns() >= end) {
      break;
    }
    __asm__ volatile("pause");
  }

  if (ec.timeouts++ == 0) {
    klog("ACPI: error: embedded controller timed out (status 0x%x)\n",
         (unsigned)inb(ec.command_port));
  } else {
    ktrace("ACPI: embedded controller timeout %zu\n", ec.timeouts);
  }
  return false;
}

static bool wait_input_empty(void)
{
  return wait_status(EC_STATUS_IBF, 0);
}

static bool wait_output_full(void)
{
  return wait_status(EC_STATUS_OBF, EC_STATUS_OBF);
}

static bool send_command(uint8_t command)
{
  if (!wait_input_empty()) {
    return false;
  }
  outb(ec.command_port, command);
  return true;
}

static bool send_data(uint8_t value)
{
  if (!wait_input_empty()) {
    return false;
  }
  outb(ec.data_port, value);
  return true;
}

static bool receive_data(uint8_t *value)
{
  if (!wait_output_full()) {
    return false;
  }
  *value = inb(ec.data_port);
  return true;
}

static bool read_byte(uint8_t address, uint8_t *value)
{
  return send_command(EC_COMMAND_READ) && send_data(address) && receive_data(value);
}

static bool write_byte(uint8_t address, uint8_t value)
{
  return send_command(EC_COMMAND_WRITE) && send_data(address) && send_data(value) &&
         wait_input_empty();
}

/* Fields wider than the EC's byte access arrive whole; they are moved one
 * byte at a time, least significant first. */
static uacpi_status region_access(uacpi_region_op op, uacpi_region_rw_data *data)
{
  if (data->offset >= EC_ADDRESS_LIMIT ||
      data->byte_width > EC_ADDRESS_LIMIT - data->offset) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }

  uint8_t address = (uint8_t)data->offset;
  if (op == UACPI_REGION_OP_READ) {
    data->value = 0;
  }
  for (uint8_t i = 0; i < data->byte_width; ++i) {
    uint8_t byte;
    if (op == UACPI_REGION_OP_READ) {
      if (!read_byte(address + i, &byte)) {
        return UACPI_STATUS_HARDWARE_TIMEOUT;
      }
      data->value |= (uint64_t)byte << (8 * i);
    } else {
      byte = (uint8_t)(data->value >> (8 * i));
      if (!write_byte(address + i, byte)) {
        return UACPI_STATUS_HARDWARE_TIMEOUT;
      }
    }
  }
  return UACPI_STATUS_OK;
}

static uacpi_status region_handler(uacpi_region_op op, uacpi_handle op_data)
{
  switch (op) {
  case UACPI_REGION_OP_ATTACH:
  case UACPI_REGION_OP_DETACH:
    return UACPI_STATUS_OK;
  case UACPI_REGION_OP_READ:
  case UACPI_REGION_OP_WRITE:
    return region_access(op, op_data);
  default:
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
}

static char hex_digit(uint8_t value)
{
  return value < 10 ? (char)('0' + value) : (char)('A' + value - 10);
}

/* Deferred work. Runs _Qxx for each pending query, then lets the GPE fire
 * again; a missing method is normal for events the firmware does not use. */
static void run_queries(uacpi_handle context)
{
  (void)context;
  ec.query_queued = false;
  for (size_t i = 0; i < EC_QUERIES_PER_EVENT; ++i) {
    if (!(inb(ec.command_port) & EC_STATUS_SCI_EVT)) {
      break;
    }
    uint8_t query;
    if (!send_command(EC_COMMAND_QUERY) || !receive_data(&query) || !query) {
      break;
    }

    char method[] = {'_', 'Q', hex_digit(query >> 4), hex_digit(query & 0xf), '\0'};
    uacpi_status status = uacpi_execute_simple(ec.node, method);
    if (status != UACPI_STATUS_OK) {
      ktrace("ACPI: embedded controller %s: %s\n", method, uacpi_status_to_string(status));
    }
  }
  uacpi_finish_handling_gpe(UACPI_NULL, ec.gpe);
}

/* SCI context, IF=0. uACPI has disabled the GPE; it stays disabled until the
 * queued queries have run. */
static uacpi_interrupt_ret gpe_handler(uacpi_handle context, uacpi_namespace_node *device,
                                       uacpi_u16 index)
{
  (void)context;
  (void)device;
  (void)index;
  if (ec.query_queued) {
    return UACPI_INTERRUPT_HANDLED;
  }
  if (uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, run_queries, UACPI_NULL) !=
      UACPI_STATUS_OK) {
    return UACPI_INTERRUPT_HANDLED | UACPI_GPE_REENABLE;
  }
  ec.query_queued = true;
  return UACPI_INTERRUPT_HANDLED;
}

static bool install_region_handler(void)
{
  uacpi_status status = uacpi_install_address_space_handler(
      ec.node, UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, region_handler, UACPI_NULL);
  if (status != UACPI_STATUS_OK) {
    klog("ACPI: error: embedded controller handler not installed: %s\n",
         uacpi_status_to_string(status));
    return false;
  }
  ec.handler_installed = true;
  return true;
}

static bool system_io_port(const struct acpi_gas *gas, uint16_t *port)
{
  if (gas->address_space_id != UACPI_ADDRESS_SPACE_SYSTEM_IO || !gas->address ||
      gas->address >= IO_PORT_LIMIT) {
    return false;
  }
  *port = (uint16_t)gas->address;
  return true;
}

void acpi_ec_load_ecdt(void)
{
  uacpi_table table;
  if (uacpi_table_find_by_signature(ACPI_ECDT_SIGNATURE, &table) != UACPI_STATUS_OK) {
    return;
  }

  struct acpi_ecdt *ecdt = table.ptr;
  size_t id_length = ecdt->hdr.length > sizeof(*ecdt) ? ecdt->hdr.length - sizeof(*ecdt) : 0;
  bool id_terminated = id_length && ecdt->ec_id[id_length - 1] == '\0';
  uacpi_namespace_node *node = UACPI_NULL;
  if (!system_io_port(&ecdt->ec_control, &ec.command_port) ||
      !system_io_port(&ecdt->ec_data, &ec.data_port) || !id_terminated ||
      uacpi_namespace_node_find(UACPI_NULL, ecdt->ec_id, &node) != UACPI_STATUS_OK) {
    klog("ACPI: ECDT not usable; looking for the embedded controller device\n");
    uacpi_table_unref(&table);
    return;
  }
  ec.node = node;
  ec.gpe = ecdt->gpe_bit;
  uacpi_table_unref(&table);

  ec.from_ecdt = install_region_handler();
}

static uacpi_iteration_decision take_first_device(void *user, uacpi_namespace_node *node,
                                                  uacpi_u32 depth)
{
  (void)depth;
  *(uacpi_namespace_node **)user = node;
  return UACPI_ITERATION_DECISION_BREAK;
}

/* The first I/O resource is the data register, the second status/command. */
static uacpi_iteration_decision take_port(void *user, uacpi_resource *resource)
{
  size_t *found = user;
  uint16_t port;
  if (resource->type == UACPI_RESOURCE_TYPE_IO) {
    port = resource->io.minimum;
  } else if (resource->type == UACPI_RESOURCE_TYPE_FIXED_IO) {
    port = resource->fixed_io.address;
  } else {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }

  if (*found == 0) {
    ec.data_port = port;
  } else {
    ec.command_port = port;
  }
  return ++*found == 2 ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

static bool load_device(void)
{
  uacpi_namespace_node *node = UACPI_NULL;
  uacpi_find_devices(EC_HID, take_first_device, &node);
  if (!node) {
    return false;
  }

  size_t ports = 0;
  uacpi_status status = uacpi_for_each_device_resource(node, "_CRS", take_port, &ports);
  uint64_t gpe;
  if (status != UACPI_STATUS_OK || ports != 2) {
    klog("ACPI: error: embedded controller has no usable _CRS\n");
    return false;
  }
  status = uacpi_eval_simple_integer(node, "_GPE", &gpe);
  if (status != UACPI_STATUS_OK || gpe > UINT16_MAX) {
    klog("ACPI: error: embedded controller _GPE unsupported: %s\n",
         uacpi_status_to_string(status));
    return false;
  }
  ec.node = node;
  ec.gpe = (uint16_t)gpe;
  return install_region_handler();
}

void acpi_ec_start(void)
{
  if (!ec.handler_installed && !load_device()) {
    return;
  }

  uint64_t global_lock;
  if (uacpi_eval_simple_integer(ec.node, "_GLK", &global_lock) == UACPI_STATUS_OK &&
      global_lock) {
    klog("ACPI: warning: embedded controller asks for the global lock; not taken\n");
  }

  uacpi_status status = uacpi_install_gpe_handler(UACPI_NULL, ec.gpe,
      UACPI_GPE_TRIGGERING_EDGE, gpe_handler, UACPI_NULL);
  if (status == UACPI_STATUS_OK) {
    status = uacpi_enable_gpe(UACPI_NULL, ec.gpe);
  }
  if (status != UACPI_STATUS_OK) {
    klog("ACPI: error: embedded controller GPE 0x%x not enabled: %s\n", (unsigned)ec.gpe,
         uacpi_status_to_string(status));
    return;
  }
  const char *path = uacpi_namespace_node_generate_absolute_path(ec.node);
  ktrace("ACPI: embedded controller %s, ports 0x%x/0x%x, GPE 0x%x (%s)\n",
         path ? path : "?", (unsigned)ec.data_port, (unsigned)ec.command_port,
         (unsigned)ec.gpe, ec.from_ecdt ? "ECDT" : EC_HID);
  if (path) {
    uacpi_free_absolute_path(path);
  }
}
