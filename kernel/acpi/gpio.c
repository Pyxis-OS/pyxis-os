/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/cpu.h>
#include <arch/intel_gpio.h>
#include <kernel/acpi/gpio.h>
#include <kernel/acpi/hid_i2c.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <uacpi/acpi.h>
#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

#define PANTHER_LAKE_GPIO_HID "INTC10BB"
#define INTEL_PLATFORM_GPIO_CID "INTC105F"
#define GPIO_DSD_REVISION UINT32_C(0x00010000)
#define GPIO_ENABLED (ACPI_STA_RESULT_DEVICE_PRESENT | \
    ACPI_STA_RESULT_DEVICE_ENABLED | ACPI_STA_RESULT_DEVICE_FUNCTIONING)

static const uint8_t properties_uuid[] = {
  0x14, 0xd8, 0xff, 0xda, 0xba, 0x6e, 0x8c, 0x4d,
  0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01,
};
static const uint8_t hierarchy_uuid[] = {
  0xe6, 0xe3, 0xb8, 0xdb, 0x86, 0x58, 0xa6, 0x4b,
  0x87, 0x95, 0x13, 0x19, 0xf5, 0x2a, 0x96, 0x6b,
};

/* The sole ACPI worker owns the capture and the one-shot attachment attempt. */
static bool attempted;

struct discovery {
  struct acpi_gpio_input snapshot;
  const char *failure;
  char detail[160];
  unsigned memory_count;
};

static bool refuse(struct discovery *discovery, const char *reason)
{
  discovery->failure = reason;
  return false;
}

/* Names are bounded, internal property keys, never firmware-provided text. */
static bool refuse_property(struct discovery *discovery, const char *name)
{
  sprintf(discovery->detail, "GPIO _DSD property missing or malformed: %s", name);
  return refuse(discovery, discovery->detail);
}

static bool equal(const char *a, const char *b)
{
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

static bool string_view(uacpi_object *object, uacpi_data_view *view)
{
  return uacpi_object_get_string(object, view) == UACPI_STATUS_OK && view->length &&
      strnlen(view->text, view->length) == view->length - 1;
}

/* _DSD UUID sections and property pairs are interpreted while their owning
 * object is alive. Only scalars and copied strings leave the ACPI worker. */
static bool section(uacpi_object *object, const uint8_t *uuid, uacpi_object **out)
{
  uacpi_object_array entries;
  *out = NULL;
  if (uacpi_object_get_package(object, &entries) != UACPI_STATUS_OK || entries.count % 2) {
    return false;
  }
  for (size_t i = 0; i < entries.count; i += 2) {
    uacpi_data_view id;
    if (uacpi_object_get_buffer(entries.objects[i], &id) != UACPI_STATUS_OK ||
        id.length != 16) {
      return false;
    }
    if (memcmp(id.bytes, uuid, 16) == 0) {
      if (*out) {
        return false;
      }
      *out = entries.objects[i + 1];
    }
  }
  return *out != NULL;
}

static bool property(uacpi_object *properties, const char *name, uacpi_object **out)
{
  uacpi_object_array entries;
  *out = NULL;
  if (uacpi_object_get_package(properties, &entries) != UACPI_STATUS_OK) {
    return false;
  }
  for (size_t i = 0; i < entries.count; ++i) {
    uacpi_object_array pair;
    uacpi_data_view key;
    if (uacpi_object_get_package(entries.objects[i], &pair) != UACPI_STATUS_OK ||
        pair.count != 2 || !string_view(pair.objects[0], &key)) {
      return false;
    }
    if (equal(key.text, name)) {
      if (*out) {
        return false;
      }
      *out = pair.objects[1];
    }
  }
  return *out != NULL;
}

static bool integer_property(struct discovery *discovery, uacpi_object *properties,
    const char *name, uint32_t *out)
{
  uacpi_object *object;
  uint64_t value;
  if (!property(properties, name, &object) ||
      uacpi_object_get_integer(object, &value) != UACPI_STATUS_OK || value > UINT32_MAX) {
    return refuse_property(discovery, name);
  }
  *out = value;
  return true;
}

static bool string_property(struct discovery *discovery, uacpi_object *properties,
    const char *name, char *out, size_t size)
{
  uacpi_object *object;
  uacpi_data_view value;
  if (!property(properties, name, &object) || !string_view(object, &value) ||
      value.length > size) {
    return refuse_property(discovery, name);
  }
  memcpy(out, value.text, value.length);
  return true;
}

static uacpi_iteration_decision capture_resource(void *user, uacpi_resource *resource)
{
  struct discovery *discovery = user;
  switch (resource->type) {
  case UACPI_RESOURCE_TYPE_FIXED_MEMORY32:
    if (++discovery->memory_count != 1 || !resource->fixed_memory32.length) {
      refuse(discovery, "missing or ambiguous GPIO memory resource");
      return UACPI_ITERATION_DECISION_BREAK;
    }
    discovery->snapshot.physical = resource->fixed_memory32.address;
    discovery->snapshot.bytes = resource->fixed_memory32.length;
    break;
  case UACPI_RESOURCE_TYPE_ADDRESS64: {
    const uacpi_resource_address64 *address = &resource->address64;
    const uacpi_resource_address_common *common = &address->common;
    kdebug("ACPI GPIO: QWordMemory type=%u consumer=%u decode=%u fixed=%u/%u "
        "cache=%u range=%u translation-kind=%u min=0x%llx max=0x%llx "
        "length=0x%llx granularity=0x%llx translation=0x%llx source-bytes=%u\n",
        common->type, common->direction, common->decode_type,
        common->fixed_min_address, common->fixed_max_address,
        common->attribute.memory.caching, common->attribute.memory.range_type,
        common->attribute.memory.translation, (unsigned long long)address->minimum,
        (unsigned long long)address->maximum, (unsigned long long)address->address_length,
        (unsigned long long)address->granularity,
        (unsigned long long)address->translation_offset, address->source.length);
    if (common->type != UACPI_RANGE_MEMORY || common->direction != UACPI_CONSUMER ||
        common->decode_type != UACPI_POSITIVE_DECODE ||
        common->fixed_min_address != UACPI_ADDRESS_FIXED ||
        common->fixed_max_address != UACPI_ADDRESS_FIXED ||
        common->attribute.memory.caching != UACPI_NON_CACHEABLE ||
        common->attribute.memory.range_type != UACPI_RANGE_TYPE_MEMORY ||
        common->attribute.memory.translation != UACPI_IO_MEM_STATIC ||
        address->granularity || address->translation_offset || address->source.length ||
        address->source.index_present) {
      refuse(discovery, "GPIO QWordMemory needs fixed, untranslated consumer MMIO");
      return UACPI_ITERATION_DECISION_BREAK;
    }
    if (++discovery->memory_count != 1 || !address->address_length ||
        address->minimum > address->maximum ||
        address->address_length > UINT64_MAX - address->minimum ||
        address->maximum - address->minimum != address->address_length - 1) {
      refuse(discovery, "GPIO QWordMemory extent missing, ambiguous or inconsistent");
      return UACPI_ITERATION_DECISION_BREAK;
    }
    discovery->snapshot.physical = address->minimum;
    discovery->snapshot.bytes = address->address_length;
    break;
  }
  case UACPI_RESOURCE_TYPE_IRQ:
  case UACPI_RESOURCE_TYPE_EXTENDED_IRQ:
  case UACPI_RESOURCE_TYPE_END_TAG:
    break;
  default:
    kdebug("ACPI GPIO: unsupported decoded _CRS resource type=%u\n", resource->type);
    refuse(discovery, "unsupported GPIO resource layout");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

static bool capture_layout(struct discovery *discovery, uacpi_namespace_node *node)
{
  uacpi_object *dsd = NULL;
  uacpi_object *properties, *hierarchy;
  uint32_t revision;
  if (uacpi_eval(node, "_DSD", NULL, &dsd) != UACPI_STATUS_OK || !dsd) {
    if (dsd) {
      uacpi_object_unref(dsd);
    }
    return refuse(discovery, "GPIO _DSD evaluation");
  }
  bool valid = section(dsd, properties_uuid, &properties) &&
      section(dsd, hierarchy_uuid, &hierarchy) &&
      integer_property(discovery, properties, "intc-gpio-sw-revision", &revision) &&
      revision == GPIO_DSD_REVISION &&
      integer_property(discovery, properties, "intc-gpio-group-count", &discovery->snapshot.group_count) &&
      discovery->snapshot.group_count &&
      discovery->snapshot.group_count <= ACPI_GPIO_GROUP_CAPACITY &&
      integer_property(discovery, properties, "intc-gpio-group-stride", &discovery->snapshot.stride) &&
      integer_property(discovery, properties, "intc-gpio-pad-ownership-offset",
          &discovery->snapshot.ownership_base);
  uacpi_object_array groups;
  valid = valid && uacpi_object_get_package(hierarchy, &groups) == UACPI_STATUS_OK &&
      groups.count == discovery->snapshot.group_count;
  uint64_t pin_base = 0;
  uacpi_namespace_node *group_nodes[ACPI_GPIO_GROUP_CAPACITY];
  bool found = false;
  for (uint32_t i = 0; valid && i < discovery->snapshot.group_count; ++i) {
    char key[sizeof("intc-gpio-group-4294967295-subproperties")];
    char child[ACPI_GPIO_PATH_CAPACITY];
    sprintf(key, "intc-gpio-group-%u-subproperties", i);
    uacpi_object *group_dsd = NULL, *group_properties;
    uacpi_object_array pair;
    uacpi_data_view entry_key;
    uacpi_namespace_node *group_node;
    uint32_t count, offset;
    char name[sizeof(discovery->snapshot.group)] = {0};
    valid = uacpi_object_get_package(groups.objects[i], &pair) == UACPI_STATUS_OK &&
        pair.count == 2 && string_view(pair.objects[0], &entry_key) &&
        equal(entry_key.text, key) && string_property(discovery, hierarchy, key, child, sizeof(child)) &&
        uacpi_namespace_node_find(node, child, &group_node) == UACPI_STATUS_OK &&
        uacpi_namespace_node_parent(group_node) == node;
    for (uint32_t j = 0; valid && j < i; ++j) {
      valid = group_nodes[j] != group_node;
    }
    if (valid) {
      group_nodes[i] = group_node;
    }
    valid = valid &&
        uacpi_eval(group_node, NULL, NULL, &group_dsd) == UACPI_STATUS_OK && group_dsd &&
        section(group_dsd, properties_uuid, &group_properties) &&
        string_property(discovery, group_properties, "intc-gpio-group-name", name, sizeof(name)) &&
        integer_property(discovery, group_properties, "intc-gpio-pad-count", &count) &&
        count && integer_property(discovery, group_properties, "intc-gpio-group-offset", &offset);
    if (valid) {
      discovery->snapshot.groups[i] = (struct acpi_gpio_group){.count = count, .offset = offset};
    }
    if (valid && discovery->snapshot.pin >= pin_base &&
        discovery->snapshot.pin - pin_base < count) {
      valid = !found;
      if (valid) {
        found = true;
        memcpy(discovery->snapshot.group, name, sizeof(name));
      }
    }
    if (valid) {
      pin_base += count;
    }
    if (group_dsd) {
      uacpi_object_unref(group_dsd);
    }
  }
  uacpi_object_unref(dsd);
  if (valid && found) {
    return true;
  }
  return discovery->failure ? false :
      refuse(discovery, "GPIO _DSD section, group reference or pin range inconsistent");
}

const struct gpio_input *acpi_gpio_input_attach(const struct acpi_hid_i2c_interrupt *interrupt)
{
  acpi_require_worker();
  if (attempted) {
    return NULL;
  }
  attempted = true;
  struct discovery discovery = {0};
  if (!interrupt || interrupt->kind != ACPI_HID_I2C_GPIO ||
      interrupt->source.index != 0 || !interrupt->source.path[0] ||
      strnlen(interrupt->source.path, sizeof(interrupt->source.path)) ==
      sizeof(interrupt->source.path)) {
    refuse(&discovery, "unsupported GPIO resource source");
    goto refused;
  }
  size_t path_bytes = strlen(interrupt->source.path) + 1;
  if (path_bytes > sizeof(discovery.snapshot.controller)) {
    refuse(&discovery, "GPIO source path capacity");
    goto refused;
  }
  memcpy(discovery.snapshot.controller, interrupt->source.path,
      path_bytes);
  discovery.snapshot.pin = interrupt->number;
  uacpi_namespace_node *node;
  uacpi_object_type type;
  uint32_t status;
  if (uacpi_namespace_node_find(NULL, interrupt->source.path, &node) != UACPI_STATUS_OK ||
      uacpi_namespace_node_type(node, &type) != UACPI_STATUS_OK || type != UACPI_OBJECT_DEVICE ||
      uacpi_eval_sta(node, &status) != UACPI_STATUS_OK || (status & GPIO_ENABLED) != GPIO_ENABLED) {
    refuse(&discovery, "GPIO source device absent or disabled");
    goto refused;
  }
  uacpi_id_string *hid = NULL;
  bool supported = uacpi_eval_hid(node, &hid) == UACPI_STATUS_OK && hid &&
      equal(hid->value, PANTHER_LAKE_GPIO_HID);
  if (hid) {
    uacpi_free_id_string(hid);
  }
  const char *const compatible[] = {INTEL_PLATFORM_GPIO_CID, NULL};
  if (!supported || !uacpi_device_matches_pnp_id(node, compatible) ||
      uacpi_eval_simple_integer(node, "_UID", &discovery.snapshot.uid) != UACPI_STATUS_OK) {
    refuse(&discovery, "unsupported GPIO hardware identity/UID");
    goto refused;
  }
  uacpi_resources *resources = NULL;
  uacpi_status result = uacpi_get_current_resources(node, &resources);
  if (result == UACPI_STATUS_OK && resources) {
    result = uacpi_for_each_resource(resources, capture_resource, &discovery);
  }
  if (resources) {
    uacpi_free_resources(resources);
  }
  if (discovery.failure || result != UACPI_STATUS_OK || discovery.memory_count != 1) {
    if (!discovery.failure) {
      refuse(&discovery, "GPIO current resources unavailable");
    }
    goto refused;
  }
  if (!capture_layout(&discovery, node)) {
    goto refused;
  }
  uint64_t flags = cpu_save_interrupts();
  const struct gpio_input *input = intel_gpio_input_attach(&discovery.snapshot);
  cpu_restore_interrupts(flags);
  return input;

refused:
  kinfo("ACPI GPIO: %s pin=%u input refused: %s\n", discovery.snapshot.controller,
      discovery.snapshot.pin, discovery.failure);
  return NULL;
}
