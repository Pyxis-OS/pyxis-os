/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/acpi/hid_i2c.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <uacpi/acpi.h>
#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

#define TOUCHPAD_HID "ELAN06FA"
#define HID_I2C_CID "PNP0C50"
#define HID_I2C_DSM_REVISION 1
#define HID_I2C_DSM_REGISTER 1
#define HID_I2C_DSM_FUNCTIONS 3u
#define ENDPOINT_ENABLED (ACPI_STA_RESULT_DEVICE_PRESENT | \
    ACPI_STA_RESULT_DEVICE_ENABLED | ACPI_STA_RESULT_DEVICE_FUNCTIONING)

static const uint8_t hid_i2c_uuid[] = {
  0xf7, 0xf6, 0xdf, 0x3c, 0x67, 0x42, 0x55, 0x45,
  0xad, 0x05, 0xb3, 0x0a, 0x3d, 0x89, 0x38, 0xde,
};

/* Written once by the ACPI worker, read only with BSP interrupts disabled. */
static enum acpi_hid_i2c_state snapshot_state;
static struct acpi_hid_i2c_endpoint published;
static uint64_t discovery_ns;

struct discovery {
  uacpi_namespace_node *node;
  struct acpi_hid_i2c_endpoint endpoint;
  const char *failure;
  uacpi_status status;
  unsigned candidates, i2c_count, interrupt_count;
};

static bool equal(const char *a, const char *b)
{
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

static bool refuse(struct discovery *discovery, const char *reason)
{
  discovery->failure = reason;
  return false;
}

static bool copy_path(struct discovery *discovery, uacpi_namespace_node *node, char *out)
{
  const char *path = uacpi_namespace_node_generate_absolute_path(node);
  if (!path) {
    return refuse(discovery, "namespace path allocation");
  }
  size_t size = strlen(path) + 1;
  bool fits = size <= ACPI_HID_I2C_PATH_CAPACITY;
  if (fits) {
    for (size_t i = 0; i < size; ++i) {
      out[i] = path[i];
    }
  }
  uacpi_free_absolute_path(path);
  return fits || refuse(discovery, "namespace path exceeds snapshot capacity");
}

static bool copy_source(struct discovery *discovery, const uacpi_resource_source *source,
    bool required, struct acpi_hid_i2c_source *out)
{
  out->index_present = source->index_present;
  out->index = source->index;
  if (!source->length || !source->string || !source->string[0]) {
    return !required || refuse(discovery, "resource source missing");
  }
  uacpi_namespace_node *node;
  discovery->status = uacpi_namespace_node_resolve_from_aml_namepath(discovery->node,
      source->string, &node);
  if (discovery->status != UACPI_STATUS_OK) {
    return refuse(discovery, "resource source resolution");
  }
  uacpi_object_type type;
  discovery->status = uacpi_namespace_node_type(node, &type);
  if (discovery->status != UACPI_STATUS_OK || type != UACPI_OBJECT_DEVICE) {
    return refuse(discovery, "resource source is not a device");
  }
  return copy_path(discovery, node, out->path);
}

static uacpi_iteration_decision capture_resource(void *user, uacpi_resource *resource)
{
  struct discovery *discovery = user;
  struct acpi_hid_i2c_endpoint *endpoint = &discovery->endpoint;
  struct acpi_hid_i2c_interrupt *interrupt = &endpoint->interrupt;
  if (resource->type == UACPI_RESOURCE_TYPE_SERIAL_I2C_CONNECTION) {
    const uacpi_resource_i2c_connection *i2c = &resource->i2c_connection;
    if (++discovery->i2c_count != 1) {
      refuse(discovery, "multiple I2C resources");
    } else if (i2c->common.mode != UACPI_MODE_CONTROLLER_INITIATED ||
        i2c->common.direction != UACPI_CONSUMER ||
        i2c->common.sharing > UACPI_SHARED || !i2c->connection_speed ||
        i2c->addressing_mode > UACPI_I2C_10BIT ||
        i2c->slave_address > (i2c->addressing_mode == UACPI_I2C_7BIT ? 0x7f : 0x3ff)) {
      refuse(discovery, "invalid I2C resource attributes");
    } else if (copy_source(discovery, &i2c->common.source, true, &endpoint->controller)) {
      endpoint->speed_hz = i2c->connection_speed;
      endpoint->address = i2c->slave_address;
      endpoint->mode = i2c->common.mode;
      endpoint->direction = i2c->common.direction;
      endpoint->sharing = i2c->common.sharing;
      endpoint->addressing_mode = i2c->addressing_mode;
    }
  } else if (resource->type == UACPI_RESOURCE_TYPE_GPIO_CONNECTION &&
      resource->gpio_connection.type == UACPI_GPIO_CONNECTION_INTERRUPT) {
    const uacpi_resource_gpio_connection *gpio = &resource->gpio_connection;
    if (++discovery->interrupt_count != 1 || gpio->pin_table_length != 1) {
      refuse(discovery, "ambiguous GPIO interrupt");
    } else if (gpio->direction != UACPI_CONSUMER ||
        gpio->intr.polarity > UACPI_POLARITY_ACTIVE_BOTH) {
      refuse(discovery, "invalid GPIO interrupt attributes");
    } else if (copy_source(discovery, &gpio->source, true, &interrupt->source)) {
      interrupt->kind = ACPI_HID_I2C_GPIO;
      interrupt->number = gpio->pin_table[0];
      interrupt->direction = gpio->direction;
      interrupt->triggering = gpio->intr.triggering;
      interrupt->polarity = gpio->intr.polarity;
      interrupt->sharing = gpio->intr.sharing;
      interrupt->wake = gpio->intr.wake_capability;
      interrupt->pull = gpio->pull_configuration;
      interrupt->drive_strength = gpio->drive_strength;
      interrupt->debounce = gpio->debounce_timeout;
    }
  } else if (resource->type == UACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
    const uacpi_resource_extended_irq *irq = &resource->extended_irq;
    if (++discovery->interrupt_count != 1 || irq->num_irqs != 1) {
      refuse(discovery, "ambiguous extended interrupt");
    } else if (irq->direction != UACPI_CONSUMER) {
      refuse(discovery, "extended interrupt is not a consumer");
    } else if (copy_source(discovery, &irq->source, false, &interrupt->source)) {
      interrupt->kind = ACPI_HID_I2C_EXTENDED_IRQ;
      interrupt->number = irq->irqs[0];
      interrupt->direction = irq->direction;
      interrupt->triggering = irq->triggering;
      interrupt->polarity = irq->polarity;
      interrupt->sharing = irq->sharing;
      interrupt->wake = irq->wake_capability;
    }
  }
  /* BREAK itself is a successful resource iteration; keep a separate failure. */
  return discovery->failure ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_status hid_dsm(uacpi_namespace_node *node, unsigned function, uacpi_object **result)
{
  uacpi_object *arguments[] = {
    uacpi_object_create_buffer((uacpi_data_view){
      .const_bytes = hid_i2c_uuid, .length = sizeof(hid_i2c_uuid),
    }),
    uacpi_object_create_integer(HID_I2C_DSM_REVISION),
    uacpi_object_create_integer(function),
    uacpi_object_create_package((uacpi_object_array){0}),
  };
  uacpi_status status = UACPI_STATUS_OUT_OF_MEMORY;
  if (arguments[0] && arguments[1] && arguments[2] && arguments[3]) {
    status = uacpi_eval(node, "_DSM", &(uacpi_object_array){
      .objects = arguments, .count = 4,
    }, result);
  }
  for (size_t i = 0; i < 4; ++i) {
    if (arguments[i]) {
      uacpi_object_unref(arguments[i]);
    }
  }
  return status;
}

static bool capture_register(struct discovery *discovery)
{
  uacpi_object *result = UACPI_NULL;
  uacpi_data_view functions;
  discovery->status = hid_dsm(discovery->node, 0, &result);
  bool supported = discovery->status == UACPI_STATUS_OK && result &&
      uacpi_object_get_buffer(result, &functions) == UACPI_STATUS_OK && functions.length &&
      (functions.const_bytes[0] & HID_I2C_DSM_FUNCTIONS) == HID_I2C_DSM_FUNCTIONS;
  if (result) {
    uacpi_object_unref(result);
  }
  if (!supported) {
    return refuse(discovery, "HID-I2C _DSM support mask");
  }
  result = UACPI_NULL;
  uint64_t value;
  discovery->status = hid_dsm(discovery->node, HID_I2C_DSM_REGISTER, &result);
  bool valid = discovery->status == UACPI_STATUS_OK && result &&
      uacpi_object_get_integer(result, &value) == UACPI_STATUS_OK && value <= UINT16_MAX;
  if (valid) {
    discovery->endpoint.descriptor_register = value;
  }
  if (result) {
    uacpi_object_unref(result);
  }
  return valid || refuse(discovery, "HID-I2C _DSM descriptor register");
}

static uacpi_iteration_decision find_endpoint(void *user, uacpi_namespace_node *node, uacpi_u32 depth)
{
  (void)depth;
  struct discovery *discovery = user;
  uacpi_id_string *hid;
  if (uacpi_eval_hid(node, &hid) != UACPI_STATUS_OK) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  bool matches = equal(hid->value, TOUCHPAD_HID);
  uacpi_free_id_string(hid);
  if (!matches) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  if (++discovery->candidates != 1) {
    refuse(discovery, "multiple ELAN endpoints");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  discovery->node = node;
  if (!copy_path(discovery, node, discovery->endpoint.path)) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  uacpi_pnp_id_list *cids;
  discovery->status = uacpi_eval_cid(node, &cids);
  if (discovery->status != UACPI_STATUS_OK) {
    refuse(discovery, "_CID evaluation");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  bool compatible = false;
  for (size_t i = 0; i < cids->num_ids; ++i) {
    compatible |= equal(cids->ids[i].value, HID_I2C_CID);
  }
  uacpi_free_pnp_id_list(cids);
  if (!compatible) {
    refuse(discovery, "PNP0C50 compatibility missing");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  discovery->status = uacpi_eval_sta(node, &discovery->endpoint.status);
  if (discovery->status != UACPI_STATUS_OK ||
      (discovery->endpoint.status & ENDPOINT_ENABLED) != ENDPOINT_ENABLED) {
    refuse(discovery, "_STA not present, enabled and functioning");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_hid_i2c_start(void)
{
  acpi_require_worker();
  KASSERT(snapshot_state == ACPI_HID_I2C_PENDING);
  uint64_t start = arch_monotonic_ns();
  struct discovery discovery = {0};
  uacpi_status status = uacpi_namespace_for_each_child(uacpi_namespace_root(), find_endpoint,
      UACPI_NULL, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &discovery);
  if (!discovery.failure && status != UACPI_STATUS_OK) {
    discovery.status = status;
    refuse(&discovery, "namespace walk");
  }
  if (discovery.node && !discovery.failure) {
    status = uacpi_for_each_device_resource(discovery.node, "_CRS", capture_resource,
        &discovery);
    if (!discovery.failure && status != UACPI_STATUS_OK) {
      discovery.status = status;
      refuse(&discovery, "_CRS evaluation");
    } else if (!discovery.failure && (discovery.i2c_count != 1 || discovery.interrupt_count != 1)) {
      refuse(&discovery, "missing I2C or interrupt resource");
    }
    if (!discovery.failure) {
      capture_register(&discovery);
    }
  }
  uint64_t flags = cpu_save_interrupts();
  discovery_ns = arch_monotonic_ns() - start;
  if (discovery.failure) {
    snapshot_state = ACPI_HID_I2C_REFUSED;
  } else if (!discovery.node) {
    snapshot_state = ACPI_HID_I2C_ABSENT;
  } else {
    published = discovery.endpoint;
    snapshot_state = ACPI_HID_I2C_READY;
  }
  cpu_restore_interrupts(flags);
  if (discovery.failure) {
    kinfo("ACPI HID-I2C: %s refused: %s (uACPI %s, %llu us)\n", discovery.endpoint.path,
        discovery.failure, uacpi_status_to_string(discovery.status),
        (unsigned long long)(discovery_ns / 1000));
  } else if (snapshot_state == ACPI_HID_I2C_READY) {
    const struct acpi_hid_i2c_endpoint *endpoint = &discovery.endpoint;
    const struct acpi_hid_i2c_interrupt *interrupt = &endpoint->interrupt;
    kinfo("ACPI HID-I2C: %s controller=%s address=0x%x %u-bit speed=%u Hz descriptor=0x%x; "
        "%s source=%s number=%u trigger=%u polarity=%u sharing=%u wake=%u (%llu us)\n",
        endpoint->path, endpoint->controller.path, endpoint->address,
        endpoint->addressing_mode == UACPI_I2C_7BIT ? 7u : 10u, endpoint->speed_hz,
        endpoint->descriptor_register, interrupt->kind == ACPI_HID_I2C_GPIO ? "GPIO" : "IRQ",
        interrupt->source.path[0] ? interrupt->source.path : "<direct>", interrupt->number,
        interrupt->triggering, interrupt->polarity, interrupt->sharing, interrupt->wake,
        (unsigned long long)(discovery_ns / 1000));
  }
}

enum acpi_hid_i2c_state acpi_hid_i2c_snapshot(struct acpi_hid_i2c_endpoint *endpoint)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (snapshot_state == ACPI_HID_I2C_READY) {
    *endpoint = published;
  }
  return snapshot_state;
}
