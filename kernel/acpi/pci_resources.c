/* SPDX-License-Identifier: MPL-2.0 */
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/acpi/pci_resources.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <uacpi/acpi.h>
#include <uacpi/namespace.h>
#include <uacpi/resources.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

#define ROOT_ENABLED (ACPI_STA_RESULT_DEVICE_PRESENT | ACPI_STA_RESULT_DEVICE_ENABLED)
#define MEMORY24_UNIT UINT64_C(256)
#define GENERIC_REGISTER_SYSTEM_MEMORY 0
#define GENERIC_REGISTER_ACCESS_QWORD 4

/* Only the worker mutates storage, while state remains PENDING. The BSP reader
 * cannot obtain it until the single final publication. No refresh is supported. */
static struct acpi_pci_resources captured;
static enum acpi_pci_resources_state snapshot_state;

struct discovery {
  const char *failure;
  char path[ACPI_PCI_RESOURCE_PATH_CAPACITY];
  uacpi_status status;
  size_t root;
  bool root_device, bus_found;
  uint64_t base_bus;
};

static bool refuse(struct discovery *discovery, const char *reason)
{
  discovery->failure = reason;
  return false;
}

static bool copy_path(struct discovery *discovery, uacpi_namespace_node *node)
{
  const char *path = uacpi_namespace_node_generate_absolute_path(node);
  if (!path) {
    return refuse(discovery, "namespace path allocation");
  }
  size_t size = strlen(path) + 1;
  bool fits = size <= sizeof(discovery->path);
  if (fits) {
    for (size_t i = 0; i < size; ++i) {
      discovery->path[i] = path[i];
    }
  }
  uacpi_free_absolute_path(path);
  return fits || refuse(discovery, "namespace path capacity");
}

static bool reserve(struct discovery *discovery, uint64_t first, uint64_t last)
{
  if (first > last) {
    return refuse(discovery, "invalid memory interval");
  }
  if (captured.reservation_count == ACPI_PCI_RESERVATION_CAPACITY) {
    return refuse(discovery, "memory reservation capacity");
  }
  struct acpi_pci_reservation *reservation =
      &captured.reservations[captured.reservation_count++];
  reservation->range = (struct acpi_pci_resource_range){first, last};
  for (size_t i = 0; i < sizeof(reservation->path); ++i) {
    reservation->path[i] = discovery->path[i];
  }
  return true;
}

/* Legacy Memory24/32 maxima bound a possible base, rather than the last byte.
 * Retain the whole envelope even when firmware describes a variable base. */
static bool reserve_memory(struct discovery *discovery, uint64_t minimum,
                           uint64_t maximum, uint64_t length)
{
  if (!length) {
    return true;
  }
  if (minimum > maximum || length - 1 > UINT64_MAX - maximum) {
    return refuse(discovery, "memory extent overflow or invalid bounds");
  }
  return reserve(discovery, minimum, maximum + length - 1);
}

static bool address_resource(struct discovery *discovery,
                              const uacpi_resource_address_common *common,
                              uint64_t minimum, uint64_t maximum,
                              uint64_t translation, uint64_t length,
                              bool extended, bool source)
{
  if (common->type == UACPI_RANGE_IO) {
    return true;
  }
  if (common->type != UACPI_RANGE_MEMORY && common->type != UACPI_RANGE_BUS) {
    return refuse(discovery, "unsupported address resource type");
  }
  if (common->type == UACPI_RANGE_BUS && !discovery->root_device) {
    return true;
  }
  if (!length) {
    return true;
  }
  if (source || translation || common->decode_type != UACPI_POSITIVE_DECODE ||
      minimum > maximum) {
    return refuse(discovery, "translated, routed or malformed address resource");
  }
  bool fixed_extent = common->fixed_min_address == UACPI_ADDRESS_FIXED &&
      common->fixed_max_address == UACPI_ADDRESS_FIXED && length - 1 == maximum - minimum;
  if (common->type == UACPI_RANGE_BUS) {
    if (!fixed_extent) {
      return refuse(discovery, "variable or inconsistent root bus range");
    }
    if ((extended && common->direction != UACPI_PRODUCER) || discovery->bus_found ||
        maximum > UINT8_MAX || minimum != discovery->base_bus) {
      return refuse(discovery, "ambiguous root bus range or _BBN mismatch");
    }
    struct acpi_pci_root *root = &captured.roots[discovery->root];
    root->first_bus = (uint8_t)minimum;
    root->last_bus = (uint8_t)maximum;
    discovery->bus_found = true;
    return true;
  }
  const uacpi_memory_attribute *memory = &common->attribute.memory;
  if (memory->translation != UACPI_IO_MEM_STATIC ||
      memory->caching > UACPI_PREFETCHABLE ||
      memory->write_status > UACPI_WRITABLE ||
      memory->range_type > UACPI_RANGE_TYPE_NVS) {
    return refuse(discovery, "unsupported memory attributes");
  }

  /* Old host descriptors' producer/consumer flag cannot identify host
   * registers reliably. Explicit device/PNP0C02 consumers are also collected.
   * Extended descriptors make the flag meaningful and can reserve registers. */
  bool window = discovery->root_device &&
      (!extended || common->direction == UACPI_PRODUCER);
  if (!window) {
    /* A reservation grants no placement authority. A variable or inconsistent
     * address descriptor can safely exclude its whole possible base envelope;
     * only a fixed exact descriptor supplies an exact end address. */
    return fixed_extent ? reserve(discovery, minimum, maximum) :
        reserve_memory(discovery, minimum, maximum, length);
  }
  if (!fixed_extent) {
    return refuse(discovery, "variable or inconsistent root aperture");
  }
  if (memory->range_type != UACPI_RANGE_TYPE_MEMORY) {
    return refuse(discovery, "non-memory root aperture attributes");
  }
  if (captured.window_count == ACPI_PCI_WINDOW_CAPACITY) {
    return refuse(discovery, "root aperture capacity");
  }
  captured.windows[captured.window_count++] = (struct acpi_pci_window){
    .range = {minimum, maximum},
    .root = discovery->root,
    .prefetchable = memory->caching == UACPI_PREFETCHABLE,
    .writable = memory->write_status == UACPI_WRITABLE,
  };
  return true;
}

static uacpi_iteration_decision capture_resource(void *opaque, uacpi_resource *resource)
{
  struct discovery *discovery = opaque;
  bool accepted = true;
  switch (resource->type) {
    case UACPI_RESOURCE_TYPE_ADDRESS16: {
      const uacpi_resource_address16 *address = &resource->address16;
      accepted = address_resource(discovery, &address->common, address->minimum,
          address->maximum, address->translation_offset, address->address_length,
          false, address->source.index_present || address->source.length);
      break;
    }
    case UACPI_RESOURCE_TYPE_ADDRESS32: {
      const uacpi_resource_address32 *address = &resource->address32;
      accepted = address_resource(discovery, &address->common, address->minimum,
          address->maximum, address->translation_offset, address->address_length,
          false, address->source.index_present || address->source.length);
      break;
    }
    case UACPI_RESOURCE_TYPE_ADDRESS64: {
      const uacpi_resource_address64 *address = &resource->address64;
      accepted = address_resource(discovery, &address->common, address->minimum,
          address->maximum, address->translation_offset, address->address_length,
          false, address->source.index_present || address->source.length);
      break;
    }
    case UACPI_RESOURCE_TYPE_ADDRESS64_EXTENDED: {
      const uacpi_resource_address64_extended *address = &resource->address64_extended;
      if (address->common.type == UACPI_RANGE_IO ||
          (address->common.type == UACPI_RANGE_BUS && !discovery->root_device)) {
        break;
      }
      if (address->revision_id != 1 || address->attributes) {
        accepted = refuse(discovery, "unsupported extended address attributes");
      } else {
        accepted = address_resource(discovery, &address->common, address->minimum,
            address->maximum, address->translation_offset, address->address_length,
            true, false);
      }
      break;
    }
    case UACPI_RESOURCE_TYPE_MEMORY24: {
      const uacpi_resource_memory24 *memory = &resource->memory24;
      accepted = reserve_memory(discovery, (uint64_t)memory->minimum * MEMORY24_UNIT,
          (uint64_t)memory->maximum * MEMORY24_UNIT, (uint64_t)memory->length * MEMORY24_UNIT);
      break;
    }
    case UACPI_RESOURCE_TYPE_MEMORY32: {
      const uacpi_resource_memory32 *memory = &resource->memory32;
      accepted = reserve_memory(discovery, memory->minimum, memory->maximum, memory->length);
      break;
    }
    case UACPI_RESOURCE_TYPE_FIXED_MEMORY32:
      accepted = reserve_memory(discovery, resource->fixed_memory32.address,
          resource->fixed_memory32.address, resource->fixed_memory32.length);
      break;
    case UACPI_RESOURCE_TYPE_GENERIC_REGISTER: {
      const uacpi_resource_generic_register *reg = &resource->generic_register;
      if (reg->address_space_id != GENERIC_REGISTER_SYSTEM_MEMORY) {
        break;
      }
      if (reg->access_size > GENERIC_REGISTER_ACCESS_QWORD || !reg->bit_width) {
        accepted = refuse(discovery, "unsupported memory register extent");
        break;
      }
      uint64_t bytes = ((uint64_t)reg->bit_offset + reg->bit_width + 7) / 8;
      uint64_t access_bytes = reg->access_size ? UINT64_C(1) << (reg->access_size - 1) : 0;
      if (bytes < access_bytes) {
        bytes = access_bytes;
      }
      accepted = reserve_memory(discovery, reg->address, reg->address, bytes);
      break;
    }
    case UACPI_RESOURCE_TYPE_START_DEPENDENT:
    case UACPI_RESOURCE_TYPE_END_DEPENDENT:
      accepted = refuse(discovery, "dependent alternatives in current resources");
      break;
    default:
      /* IRQ, port, DMA, GPIO, serial bus, pin and opaque vendor descriptors
       * do not supply a standard memory-address extent to this collector. */
      break;
  }
  return accepted ? UACPI_ITERATION_DECISION_CONTINUE : UACPI_ITERATION_DECISION_BREAK;
}

static bool root_integer(struct discovery *discovery, uacpi_namespace_node *node,
                          const char *method, uint64_t limit, uint64_t *value)
{
  uacpi_status status = uacpi_eval_integer(node, method, UACPI_NULL, value);
  if (status == UACPI_STATUS_NOT_FOUND) {
    *value = 0;
    return true;
  }
  if (status != UACPI_STATUS_OK || *value > limit) {
    discovery->status = status;
    return refuse(discovery, "invalid root _SEG or _BBN");
  }
  return true;
}

static bool equal(const char *a, const char *b)
{
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

static void classify_id(const char *id, bool *root)
{
  *root |= equal(id, "PNP0A03") || equal(id, "PNP0A08");
}

/* The convenience PNP matcher hides evaluation failures. A failed ID method
 * must not silently turn a root into an ordinary device. */
static bool classify_device(struct discovery *discovery, uacpi_namespace_node *node,
                            bool *root)
{
  uacpi_id_string *hid = NULL;
  uacpi_status status = uacpi_eval_hid(node, &hid);
  if (status == UACPI_STATUS_OK) {
    classify_id(hid->value, root);
  }
  uacpi_free_id_string(hid);
  if (status != UACPI_STATUS_OK && status != UACPI_STATUS_NOT_FOUND) {
    discovery->status = status;
    return refuse(discovery, "_HID evaluation");
  }
  uacpi_pnp_id_list *cid = NULL;
  status = uacpi_eval_cid(node, &cid);
  if (status == UACPI_STATUS_OK) {
    for (size_t i = 0; i < cid->num_ids; ++i) {
      classify_id(cid->ids[i].value, root);
    }
  }
  uacpi_free_pnp_id_list(cid);
  if (status != UACPI_STATUS_OK && status != UACPI_STATUS_NOT_FOUND) {
    discovery->status = status;
    return refuse(discovery, "_CID evaluation");
  }
  return true;
}

static uacpi_iteration_decision capture_device(void *opaque, uacpi_namespace_node *node,
                                               uacpi_u32 depth)
{
  (void)depth;
  struct discovery *discovery = opaque;
  if (!copy_path(discovery, node)) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  uacpi_u32 flags;
  discovery->status = uacpi_eval_sta(node, &flags);
  if (discovery->status != UACPI_STATUS_OK) {
    refuse(discovery, "_STA evaluation");
    return UACPI_ITERATION_DECISION_BREAK;
  }
  bool root_id = false;
  if (!classify_device(discovery, node, &root_id)) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  discovery->root_device = root_id && (flags & ROOT_ENABLED) == ROOT_ENABLED;
  discovery->bus_found = false;
  if (discovery->root_device) {
    if (captured.root_count == ACPI_PCI_ROOT_CAPACITY) {
      refuse(discovery, "root capacity");
      return UACPI_ITERATION_DECISION_BREAK;
    }
    uint64_t segment;
    if (!root_integer(discovery, node, "_SEG", UINT16_MAX, &segment) ||
        !root_integer(discovery, node, "_BBN", UINT8_MAX, &discovery->base_bus)) {
      return UACPI_ITERATION_DECISION_BREAK;
    }
    discovery->root = captured.root_count++;
    struct acpi_pci_root *root = &captured.roots[discovery->root];
    root->segment = (uint16_t)segment;
    for (size_t i = 0; i < sizeof(root->path); ++i) {
      root->path[i] = discovery->path[i];
    }
  }
  discovery->status = uacpi_for_each_device_resource(node, "_CRS", capture_resource, discovery);
  bool absent = discovery->status == UACPI_STATUS_NOT_FOUND;
  if (!discovery->failure && discovery->status != UACPI_STATUS_OK &&
      !(absent && !discovery->root_device)) {
    refuse(discovery, "_CRS evaluation or decoding");
  }
  if (!discovery->failure && discovery->root_device && !discovery->bus_found) {
    refuse(discovery, "missing root bus range");
  }
  return discovery->failure ? UACPI_ITERATION_DECISION_BREAK : UACPI_ITERATION_DECISION_CONTINUE;
}

void acpi_pci_resources_start(void)
{
  acpi_require_worker();
  KASSERT(snapshot_state == ACPI_PCI_RESOURCES_PENDING);
  struct discovery discovery = {0};
  uacpi_status status = uacpi_namespace_for_each_child(uacpi_namespace_root(), capture_device,
      UACPI_NULL, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &discovery);
  if (!discovery.failure && status != UACPI_STATUS_OK) {
    discovery.status = status;
    refuse(&discovery, "namespace walk");
  }
  if (!discovery.failure && (!captured.root_count || !captured.window_count)) {
    refuse(&discovery, "missing root or memory aperture");
  }
  uint64_t flags = cpu_save_interrupts();
  snapshot_state = discovery.failure ? ACPI_PCI_RESOURCES_REFUSED : ACPI_PCI_RESOURCES_READY;
  cpu_restore_interrupts(flags);
  if (discovery.failure) {
    kinfo("ACPI PCI resources: %s refused: %s (uACPI %s)\n", discovery.path,
        discovery.failure, uacpi_status_to_string(discovery.status));
  } else {
    kdebug("ACPI PCI resources: %zu roots, %zu apertures, %zu memory reservations\n",
        captured.root_count, captured.window_count, captured.reservation_count);
  }
}

enum acpi_pci_resources_state acpi_pci_resources_snapshot(
    const struct acpi_pci_resources **resources)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  if (snapshot_state == ACPI_PCI_RESOURCES_READY) {
    *resources = &captured;
  }
  return snapshot_state;
}
