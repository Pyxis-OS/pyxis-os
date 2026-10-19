/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Intel Wi-Fi ACPI table layouts and interpretation reference Linux v6.12
 * iwlwifi fw/acpi.c, acpi.h and regulatory.h under their BSD alternative.
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name Intel Corporation nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/acpi/pci_resources.h>
#include <kernel/acpi/wifi.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <uacpi/acpi.h>
#include <uacpi/namespace.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include "host.h"

#define AX200_VENDOR 0x8086
#define AX200_DEVICE 0x2723
#define WIFI_DOMAIN 7
#define WIFI_ENABLED (ACPI_STA_RESULT_DEVICE_PRESENT | ACPI_STA_RESULT_DEVICE_ENABLED)
#define SAR_ENABLED 1u
#define GLAI_UNLOCKED 0
#define GLAI_MAXIMUM 2
#define DOMAIN_PACKAGE_CAPACITY 64
#define NAMESPACE_VISIT_CAPACITY 4096
#define PPAG_MINIMUM_GAIN (-16)
#define PPAG_LOW_MAXIMUM_GAIN 24
#define PPAG_HIGH_MAXIMUM_GAIN 40

/* Only the ACPI worker writes this storage, before the final publication. */
static struct acpi_wifi_policy captured;
static enum acpi_wifi_state snapshot_state;

static enum acpi_wifi_state evaluate(uacpi_namespace_node *node, const char *method,
                                     const uacpi_object_array *arguments,
                                     uacpi_object **object);

static enum acpi_wifi_state node_integer(uacpi_namespace_node *node, const char *method,
                                         uint64_t maximum, uint64_t *value)
{
  uacpi_object *object = NULL;
  enum acpi_wifi_state state = evaluate(node, method, NULL, &object);
  if (state == ACPI_WIFI_VALID &&
      (uacpi_object_get_integer(object, value) != UACPI_STATUS_OK || *value > maximum)) {
    state = ACPI_WIFI_MALFORMED;
  }
  if (object) {
    uacpi_object_unref(object);
  }
  return state;
}

static void source_states(enum acpi_wifi_state state)
{
  captured.glai.state = state;
  captured.sar.state = state;
  captured.ewrd.state = state;
  captured.geo.state = state;
  captured.ppag.state = state;
  captured.dsm.state = state;
  for (size_t i = 0; i < ACPI_WIFI_DSM_FUNCTIONS; ++i) {
    captured.dsm.function[i].state = state;
  }
}

static void publish(enum acpi_wifi_state state)
{
  uint64_t flags = cpu_save_interrupts();
  snapshot_state = state;
  cpu_restore_interrupts(flags);
}

void acpi_wifi_unavailable(void)
{
  source_states(ACPI_WIFI_FAILED);
  publish(ACPI_WIFI_FAILED);
}

struct companion_search {
  uint64_t address;
  size_t visits;
  uacpi_namespace_node *match;
  enum acpi_wifi_state state;
};

static uacpi_iteration_decision find_address(void *opaque, uacpi_namespace_node *node,
                                             uacpi_u32 depth)
{
  (void)depth;
  struct companion_search *search = opaque;
  if (++search->visits > NAMESPACE_VISIT_CAPACITY) {
    search->state = ACPI_WIFI_UNSUPPORTED;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  uacpi_bool is_device;
  if (uacpi_namespace_node_is(node, UACPI_OBJECT_DEVICE, &is_device) != UACPI_STATUS_OK) {
    search->state = ACPI_WIFI_FAILED;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if (!is_device) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  uint64_t address;
  enum acpi_wifi_state state = node_integer(node, "_ADR", UINT64_MAX, &address);
  if (state == ACPI_WIFI_ABSENT) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  if (state != ACPI_WIFI_VALID) {
    search->state = state;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if (address >> 32 || (address >> 16) >= PCI_DEVICE_COUNT ||
      (address & UINT16_MAX) >= PCI_FUNCTION_COUNT) {
    search->state = ACPI_WIFI_MALFORMED;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if (address == search->address) {
    if (search->match) {
      search->state = ACPI_WIFI_MALFORMED;
      return UACPI_ITERATION_DECISION_BREAK;
    }
    search->match = node;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

static enum acpi_wifi_state find_companion(const struct pci_device *device,
                                           uacpi_namespace_node **companion)
{
  const struct pci_device *ancestry[PCI_BUS_COUNT];
  size_t count = 0;
  for (const struct pci_device *at = device; at; at = at->parent) {
    if (count == PCI_BUS_COUNT) {
      return ACPI_WIFI_UNSUPPORTED;
    }
    ancestry[count++] = at;
  }
  const struct acpi_pci_resources *resources = NULL;
  uint64_t flags = cpu_save_interrupts();
  enum acpi_pci_resources_state state = acpi_pci_resources_snapshot(&resources);
  cpu_restore_interrupts(flags);
  if (state != ACPI_PCI_RESOURCES_READY) {
    return ACPI_WIFI_FAILED;
  }
  const struct acpi_pci_root *root = NULL;
  for (size_t i = 0; i < resources->root_count; ++i) {
    const struct acpi_pci_root *candidate = &resources->roots[i];
    if (candidate->segment == 0 && candidate->first_bus == ancestry[count - 1]->address.bus) {
      if (root) {
        return ACPI_WIFI_MALFORMED;
      }
      root = candidate;
    }
  }
  if (!root) {
    return ACPI_WIFI_FAILED;
  }
  uacpi_namespace_node *parent;
  if (uacpi_namespace_node_find(NULL, root->path, &parent) != UACPI_STATUS_OK) {
    return ACPI_WIFI_FAILED;
  }
  struct companion_search search = {0};
  while (count) {
    const struct pci_device *at = ancestry[--count];
    search.address = ((uint64_t)at->address.device << 16) | at->address.function;
    search.match = NULL;
    search.state = ACPI_WIFI_VALID;
    uacpi_status status = uacpi_namespace_for_each_child(parent, find_address, NULL,
        UACPI_OBJECT_ANY_BIT, 1, &search);
    if (search.state != ACPI_WIFI_VALID) {
      return search.state;
    }
    if (status != UACPI_STATUS_OK) {
      return ACPI_WIFI_FAILED;
    }
    if (!search.match) {
      return ACPI_WIFI_ABSENT;
    }
    uint64_t sta;
    enum acpi_wifi_state sta_state = node_integer(search.match, "_STA", UINT32_MAX, &sta);
    if (sta_state == ACPI_WIFI_ABSENT) {
      /* ACPI's specified default only when the _STA object does not exist. */
      sta = UINT32_MAX;
    } else if (sta_state != ACPI_WIFI_VALID) {
      return sta_state;
    }
    if ((sta & WIFI_ENABLED) != WIFI_ENABLED) {
      return ACPI_WIFI_DISABLED;
    }
    parent = search.match;
  }
  *companion = parent;
  return ACPI_WIFI_VALID;
}

/* Resolve first: NOT_FOUND returned by a present AML method is a failed
 * evaluation, and must never become permission to use an absent-table default. */
static enum acpi_wifi_state evaluate(uacpi_namespace_node *node, const char *method,
                                     const uacpi_object_array *arguments,
                                     uacpi_object **object)
{
  uacpi_namespace_node *source;
  uacpi_status status = uacpi_namespace_node_find(node, method, &source);
  if (status == UACPI_STATUS_NOT_FOUND) {
    return ACPI_WIFI_ABSENT;
  }
  if (status != UACPI_STATUS_OK ||
      uacpi_eval(source, NULL, arguments, object) != UACPI_STATUS_OK) {
    return ACPI_WIFI_FAILED;
  }
  return *object ? ACPI_WIFI_VALID : ACPI_WIFI_MALFORMED;
}

static bool integer(const uacpi_object_array *array, size_t index, uint64_t maximum,
                      uint64_t *value)
{
  return index < array->count &&
      uacpi_object_get_integer(array->objects[index], value) == UACPI_STATUS_OK &&
      *value <= maximum;
}

static enum acpi_wifi_state wifi_package(uacpi_object *object, uint8_t *revision,
                                         uacpi_object_array *wifi)
{
  uacpi_object_array outer;
  uint64_t value;
  if (uacpi_object_get_package(object, &outer) != UACPI_STATUS_OK || outer.count < 2 ||
      !integer(&outer, 0, UINT64_MAX, &value)) {
    return ACPI_WIFI_MALFORMED;
  }
  if (outer.count > DOMAIN_PACKAGE_CAPACITY || value > UINT8_MAX) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  *revision = value;
  bool found = false;
  for (size_t i = 1; i < outer.count; ++i) {
    uacpi_object_array domain;
    if (uacpi_object_get_package(outer.objects[i], &domain) != UACPI_STATUS_OK ||
        !integer(&domain, 0, UINT64_MAX, &value)) {
      return ACPI_WIFI_MALFORMED;
    }
    if (value != WIFI_DOMAIN) {
      continue;
    }
    if (found) {
      return ACPI_WIFI_MALFORMED;
    }
    *wifi = domain;
    found = true;
  }
  return found ? ACPI_WIFI_VALID : ACPI_WIFI_ABSENT;
}

static bool sar_values(const uacpi_object_array *package, size_t *index,
                        uint8_t limits[ACPI_WIFI_SAR_CHAINS][ACPI_WIFI_SAR_BANDS],
                        unsigned first_chain, unsigned chains, unsigned bands)
{
  for (unsigned chain = first_chain; chain < first_chain + chains; ++chain) {
    for (unsigned band = 0; band < bands; ++band) {
      uint64_t value;
      if (!integer(package, (*index)++, UINT8_MAX, &value)) {
        return false;
      }
      limits[chain][band] = value;
    }
  }
  return true;
}

static enum acpi_wifi_state capture_sar(const uacpi_object_array *package)
{
  struct acpi_wifi_sar *sar = &captured.sar;
  if (sar->revision > 2) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  sar->chains = sar->revision == 2 ? 4 : 2;
  sar->bands = sar->revision == 0 ? 5 : 11;
  uint64_t flags;
  if (package->count != 2u + sar->chains * sar->bands ||
      !integer(package, 1, UINT32_MAX, &flags)) {
    return ACPI_WIFI_MALFORMED;
  }
  sar->flags = flags;
  size_t index = 2;
  if (!sar_values(package, &index, sar->limits, 0, sar->chains, sar->bands)) {
    return ACPI_WIFI_MALFORMED;
  }
  return flags & SAR_ENABLED ? ACPI_WIFI_VALID : ACPI_WIFI_DISABLED;
}

static enum acpi_wifi_state capture_ewrd(const uacpi_object_array *package)
{
  struct acpi_wifi_ewrd *ewrd = &captured.ewrd;
  if (ewrd->revision > 2) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  ewrd->chains = ewrd->revision == 2 ? 4 : 2;
  ewrd->bands = ewrd->revision == 0 ? 5 : 11;
  uint64_t enabled, profiles;
  if (package->count != 3u + 3u * ewrd->chains * ewrd->bands ||
      !integer(package, 1, UINT32_MAX, &enabled) || !integer(package, 2, 3, &profiles)) {
    return ACPI_WIFI_MALFORMED;
  }
  ewrd->profiles = profiles;
  size_t index = 3;
  for (unsigned profile = 0; profile < profiles; ++profile) {
    if (!sar_values(package, &index, ewrd->limits[profile], 0, 2, ewrd->bands)) {
      return ACPI_WIFI_MALFORMED;
    }
  }
  if (ewrd->revision == 2) {
    for (unsigned profile = 0; profile < profiles; ++profile) {
      if (!sar_values(package, &index, ewrd->limits[profile], 2, 2, ewrd->bands)) {
        return ACPI_WIFI_MALFORMED;
      }
    }
  }
  return enabled && profiles ? ACPI_WIFI_VALID : ACPI_WIFI_DISABLED;
}

static enum acpi_wifi_state capture_geo(const uacpi_object_array *package)
{
  struct acpi_wifi_geo *geo = &captured.geo;
  if (geo->revision > 3) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  geo->bands = geo->revision < 2 ? 2 : 3;
  geo->profiles = geo->revision == 3 ? 8 : 3;
  size_t index = geo->revision == 3 ? 2 : 1;
  if (package->count != index + 3u * geo->bands * geo->profiles) {
    return ACPI_WIFI_MALFORMED;
  }
  if (geo->revision == 3) {
    uint64_t profiles;
    if (!integer(package, 1, 8, &profiles) || profiles < 3) {
      return ACPI_WIFI_MALFORMED;
    }
    geo->profiles = profiles;
  }
  for (unsigned profile = 0; profile < geo->profiles; ++profile) {
    for (unsigned band = 0; band < geo->bands; ++band) {
      uint64_t maximum, a, b;
      if (!integer(package, index++, UINT8_MAX, &maximum) ||
          !integer(package, index++, UINT8_MAX, &a) ||
          !integer(package, index++, UINT8_MAX, &b)) {
        return ACPI_WIFI_MALFORMED;
      }
      geo->table[profile][band] = (struct acpi_wifi_geo_band){maximum, a, b};
    }
  }
  return ACPI_WIFI_VALID;
}

static enum acpi_wifi_state capture_ppag(const uacpi_object_array *package)
{
  struct acpi_wifi_ppag *ppag = &captured.ppag;
  if (ppag->revision > 3) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  ppag->bands = ppag->revision == 0 ? 5 : 11;
  uint64_t flags;
  if (package->count != 2u + 2u * ppag->bands ||
      !integer(package, 1, UINT32_MAX, &flags)) {
    return ACPI_WIFI_MALFORMED;
  }
  ppag->flags = flags & (ppag->revision < 3 ? 3u : 0x7ffu);
  size_t index = 2;
  for (unsigned chain = 0; chain < 2; ++chain) {
    for (unsigned band = 0; band < ppag->bands; ++band) {
      uint64_t value;
      if (!integer(package, index++, UINT64_MAX, &value)) {
        return ACPI_WIFI_MALFORMED;
      }
      /* Accept a raw signed byte or an AML sign-extended negative integer. */
      if (value > UINT8_MAX && value < UINT64_MAX - 127) {
        return ACPI_WIFI_MALFORMED;
      }
      int gain = (int8_t)value;
      int maximum = band == 0 ? PPAG_LOW_MAXIMUM_GAIN : PPAG_HIGH_MAXIMUM_GAIN;
      if (gain < PPAG_MINIMUM_GAIN || gain > maximum) {
        return ACPI_WIFI_MALFORMED;
      }
      ppag->gain[chain][band] = gain;
    }
  }
  return ppag->flags ? ACPI_WIFI_VALID : ACPI_WIFI_DISABLED;
}

enum table_kind { TABLE_GLAI, TABLE_SAR, TABLE_EWRD, TABLE_GEO, TABLE_PPAG };

static enum acpi_wifi_state capture_table(uacpi_namespace_node *node,
                                          const char *method, enum table_kind kind)
{
  uacpi_object *object = NULL;
  enum acpi_wifi_state state = evaluate(node, method, NULL, &object);
  uacpi_object_array package;
  uint8_t revision = 0;
  if (state == ACPI_WIFI_VALID) {
    state = wifi_package(object, &revision, &package);
  }
  if (state == ACPI_WIFI_VALID) {
    switch (kind) {
      case TABLE_GLAI: {
        uint64_t value;
        if (revision != 0) {
          state = ACPI_WIFI_UNSUPPORTED;
        } else if (package.count != 2 || !integer(&package, 1, GLAI_MAXIMUM, &value)) {
          state = ACPI_WIFI_MALFORMED;
        } else {
          captured.glai.value = value;
        }
        break;
      }
      case TABLE_SAR:
        captured.sar.revision = revision;
        state = capture_sar(&package);
        break;
      case TABLE_EWRD:
        captured.ewrd.revision = revision;
        state = capture_ewrd(&package);
        break;
      case TABLE_GEO:
        captured.geo.revision = revision;
        state = capture_geo(&package);
        break;
      case TABLE_PPAG:
        captured.ppag.revision = revision;
        state = capture_ppag(&package);
        break;
    }
  }
  if (object) {
    uacpi_object_unref(object);
  }
  ktrace("ACPI Wi-Fi: %s capture state=%u revision=%u\n", method, (unsigned)state, revision);
  return state;
}

static enum acpi_wifi_state dsm_integer(uacpi_namespace_node *node, unsigned function,
                                        unsigned bytes, uint32_t *value)
{
  static const uint8_t uuid[] = {
    0xbf, 0x02, 0x12, 0xf2, 0x78, 0x8f, 0xc6, 0x4d,
    0xa5, 0xb3, 0x1f, 0x73, 0x8e, 0x28, 0x5a, 0xde,
  };
  uacpi_object *arguments[] = {
    uacpi_object_create_buffer((uacpi_data_view){.const_bytes = uuid, .length = sizeof(uuid)}),
    uacpi_object_create_integer(0),
    uacpi_object_create_integer(function),
    uacpi_object_create_package((uacpi_object_array){0}),
  };
  enum acpi_wifi_state state = ACPI_WIFI_FAILED;
  uacpi_object *object = NULL;
  if (arguments[0] && arguments[1] && arguments[2] && arguments[3]) {
    state = evaluate(node, "_DSM", &(uacpi_object_array){.objects = arguments, .count = 4},
        &object);
  }
  if (state == ACPI_WIFI_VALID) {
    uint64_t result;
    if (uacpi_object_get_integer(object, &result) == UACPI_STATUS_OK) {
      if (result > (bytes == 1 ? UINT8_MAX : UINT32_MAX)) {
        state = ACPI_WIFI_MALFORMED;
      } else {
        *value = result;
      }
    } else {
      uacpi_data_view buffer;
      if (uacpi_object_get_buffer(object, &buffer) != UACPI_STATUS_OK ||
          !buffer.length || buffer.length > bytes) {
        state = ACPI_WIFI_MALFORMED;
      } else {
        *value = 0;
        for (size_t i = 0; i < buffer.length; ++i) {
          *value |= (uint32_t)buffer.bytes[i] << (8 * i);
        }
      }
    }
  }
  if (object) {
    uacpi_object_unref(object);
  }
  for (size_t i = 0; i < 4; ++i) {
    if (arguments[i]) {
      uacpi_object_unref(arguments[i]);
    }
  }
  return state;
}

static void capture_dsm(uacpi_namespace_node *node)
{
  captured.dsm.state = dsm_integer(node, 0, 4, &captured.dsm.supported);
  captured.dsm.function[0] = (struct acpi_wifi_integer){
    captured.dsm.state, captured.dsm.supported,
  };
  ktrace("ACPI Wi-Fi: _DSM query state=%u\n", (unsigned)captured.dsm.state);
  if (captured.dsm.state != ACPI_WIFI_VALID) {
    for (size_t i = 1; i < ACPI_WIFI_DSM_FUNCTIONS; ++i) {
      captured.dsm.function[i].state = captured.dsm.state;
    }
    return;
  }
  if (!(captured.dsm.supported & 1u)) {
    captured.dsm.state = ACPI_WIFI_DISABLED;
    for (size_t i = 1; i < ACPI_WIFI_DSM_FUNCTIONS; ++i) {
      captured.dsm.function[i].state = ACPI_WIFI_ABSENT;
    }
    return;
  }
  for (size_t i = 1; i < ACPI_WIFI_DSM_FUNCTIONS; ++i) {
    captured.dsm.function[i].state = captured.dsm.supported & (1u << i) ?
        ACPI_WIFI_UNSUPPORTED : ACPI_WIFI_ABSENT;
  }
  static const unsigned functions[] = {1, 2, 3, 4, 6, 7, 8, 9};
  for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
    unsigned function = functions[i];
    struct acpi_wifi_integer *result = &captured.dsm.function[function];
    if (!(captured.dsm.supported & (1u << function))) {
      result->state = ACPI_WIFI_ABSENT;
      continue;
    }
    result->state = dsm_integer(node, function, function <= 2 ? 1 : 4, &result->value);
    /* A supported function disappearing is a failure, not absence. */
    if (result->state == ACPI_WIFI_ABSENT) {
      result->state = ACPI_WIFI_FAILED;
    }
    ktrace("ACPI Wi-Fi: _DSM function=%u state=%u\n", function, (unsigned)result->state);
  }
}

void acpi_wifi_start(void)
{
  acpi_require_worker();
  KASSERT(snapshot_state == ACPI_WIFI_PENDING);
  source_states(ACPI_WIFI_ABSENT);
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(AX200_VENDOR, AX200_DEVICE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    enum acpi_wifi_state state = selection == PCI_SELECTION_ABSENT ? ACPI_WIFI_ABSENT :
        selection == PCI_SELECTION_AMBIGUOUS ? ACPI_WIFI_UNSUPPORTED : ACPI_WIFI_FAILED;
    source_states(state);
    publish(state);
    return;
  }
  captured.address = device->address;
  uacpi_namespace_node *node = NULL;
  enum acpi_wifi_state state = find_companion(device, &node);
  if (state != ACPI_WIFI_VALID) {
    source_states(state);
    publish(state);
    ktrace("ACPI Wi-Fi: PCI companion state=%u\n", (unsigned)state);
    return;
  }
  captured.glai.state = capture_table(node, "GLAI", TABLE_GLAI);
  if ((captured.glai.state != ACPI_WIFI_ABSENT && captured.glai.state != ACPI_WIFI_VALID) ||
      (captured.glai.state == ACPI_WIFI_VALID && captured.glai.value != GLAI_UNLOCKED)) {
    enum acpi_wifi_state source = captured.glai.state == ACPI_WIFI_VALID ?
        ACPI_WIFI_UNSUPPORTED : captured.glai.state;
    /* The authoritative locked UEFI tables cannot be read by this kernel. */
    captured.sar.state = captured.ewrd.state = captured.geo.state = source;
    captured.ppag.state = captured.dsm.state = source;
    for (size_t i = 0; i < ACPI_WIFI_DSM_FUNCTIONS; ++i) {
      captured.dsm.function[i].state = source;
    }
    publish(source);
    return;
  }
  captured.sar.state = capture_table(node, "WRDS", TABLE_SAR);
  captured.ewrd.state = capture_table(node, "EWRD", TABLE_EWRD);
  captured.geo.state = capture_table(node, "WGDS", TABLE_GEO);
  captured.ppag.state = capture_table(node, "PPAG", TABLE_PPAG);
  capture_dsm(node);
  publish(ACPI_WIFI_VALID);
}

enum acpi_wifi_state acpi_wifi_snapshot(struct acpi_wifi_policy *policy)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (snapshot_state != ACPI_WIFI_PENDING) {
    *policy = captured;
  }
  return snapshot_state;
}
