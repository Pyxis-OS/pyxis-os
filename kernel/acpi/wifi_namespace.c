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
#include <kernel/log.h>
#include <kernel/memory.h>
#include <uacpi/acpi.h>
#include <uacpi/utilities.h>
#include "wifi_namespace.h"

#define WIFI_ENABLED (ACPI_STA_RESULT_DEVICE_PRESENT | ACPI_STA_RESULT_DEVICE_ENABLED)
#define DOMAIN_PACKAGE_CAPACITY 64
#define NAMESPACE_VISIT_CAPACITY 4096

static enum acpi_wifi_state node_integer(uacpi_namespace_node *node, const char *method,
                                         uint64_t maximum, uint64_t *value)
{
  uacpi_object *object = NULL;
  enum acpi_wifi_state state = acpi_wifi_evaluate(node, method, NULL, &object);
  if (state == ACPI_WIFI_VALID &&
      (uacpi_object_get_integer(object, value) != UACPI_STATUS_OK || *value > maximum)) {
    state = ACPI_WIFI_MALFORMED;
  }
  if (object) {
    uacpi_object_unref(object);
  }
  return state;
}

struct root_search {
  unsigned bus;
  size_t visits;
  uacpi_namespace_node *match;
  enum acpi_wifi_state state;
};

static bool root_id(const char *id)
{
  static const char pci[] = "PNP0A03", pcie[] = "PNP0A08";
  size_t i = 0;
  while (i < sizeof(pci) && id[i]) {
    ++i;
  }
  return i == sizeof(pci) - 1 &&
      (!memcmp(id, pci, sizeof(pci)) || !memcmp(id, pcie, sizeof(pcie)));
}

static enum acpi_wifi_state root_identity(uacpi_namespace_node *node, bool *root)
{
  /* Resolve before evaluation so a broken present method cannot become absence. */
  uacpi_namespace_node *source;
  uacpi_status status = uacpi_namespace_node_find(node, "_HID", &source);
  if (status == UACPI_STATUS_OK) {
    uacpi_id_string *hid = NULL;
    status = uacpi_eval_hid(node, &hid);
    if (status == UACPI_STATUS_OK) {
      *root |= root_id(hid->value);
    }
    uacpi_free_id_string(hid);
  } else if (status == UACPI_STATUS_NOT_FOUND) {
    status = UACPI_STATUS_OK;
  }
  if (status != UACPI_STATUS_OK) {
    return ACPI_WIFI_FAILED;
  }
  status = uacpi_namespace_node_find(node, "_CID", &source);
  if (status == UACPI_STATUS_NOT_FOUND) {
    return ACPI_WIFI_VALID;
  }
  if (status != UACPI_STATUS_OK) {
    return ACPI_WIFI_FAILED;
  }
  uacpi_pnp_id_list *cid = NULL;
  status = uacpi_eval_cid(node, &cid);
  enum acpi_wifi_state state = status == UACPI_STATUS_OK ? ACPI_WIFI_VALID : ACPI_WIFI_FAILED;
  if (state == ACPI_WIFI_VALID) {
    if (cid->num_ids > DOMAIN_PACKAGE_CAPACITY) {
      state = ACPI_WIFI_UNSUPPORTED;
    } else {
      for (size_t i = 0; i < cid->num_ids; ++i) {
        *root |= root_id(cid->ids[i].value);
      }
    }
  }
  uacpi_free_pnp_id_list(cid);
  return state;
}

static uacpi_iteration_decision find_root(void *opaque, uacpi_namespace_node *node,
                                          uacpi_u32 depth)
{
  (void)depth;
  struct root_search *search = opaque;
  if (++search->visits > NAMESPACE_VISIT_CAPACITY) {
    search->state = ACPI_WIFI_UNSUPPORTED;
    return UACPI_ITERATION_DECISION_BREAK;
  }
  bool root = false;
  search->state = root_identity(node, &root);
  if (search->state != ACPI_WIFI_VALID) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if (!root) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  uint64_t sta, segment, bus;
  search->state = node_integer(node, "_STA", UINT32_MAX, &sta);
  if (search->state == ACPI_WIFI_ABSENT) {
    sta = UINT32_MAX;
    search->state = ACPI_WIFI_VALID;
  }
  if (search->state != ACPI_WIFI_VALID) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if ((sta & WIFI_ENABLED) != WIFI_ENABLED) {
    return UACPI_ITERATION_DECISION_CONTINUE;
  }
  search->state = node_integer(node, "_SEG", UINT16_MAX, &segment);
  if (search->state == ACPI_WIFI_ABSENT) {
    segment = 0;
    search->state = ACPI_WIFI_VALID;
  }
  if (search->state != ACPI_WIFI_VALID) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  search->state = node_integer(node, "_BBN", UINT8_MAX, &bus);
  if (search->state == ACPI_WIFI_ABSENT) {
    bus = 0;
    search->state = ACPI_WIFI_VALID;
  }
  if (search->state != ACPI_WIFI_VALID) {
    return UACPI_ITERATION_DECISION_BREAK;
  }
  if (segment == 0 && bus == search->bus) {
    if (search->match) {
      search->state = ACPI_WIFI_MALFORMED;
      return UACPI_ITERATION_DECISION_BREAK;
    }
    search->match = node;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

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
  if (address == search->address) {
    uint64_t sta;
    state = node_integer(node, "_STA", UINT32_MAX, &sta);
    if (state == ACPI_WIFI_ABSENT) {
      sta = UINT32_MAX;
    } else if (state != ACPI_WIFI_VALID) {
      search->state = state;
      return UACPI_ITERATION_DECISION_BREAK;
    }
    if ((sta & WIFI_ENABLED) != WIFI_ENABLED) {
      search->disabled = true;
      return UACPI_ITERATION_DECISION_CONTINUE;
    }
    if (search->count == ENDPOINT_ALIAS_CAPACITY) {
      search->state = ACPI_WIFI_UNSUPPORTED;
      return UACPI_ITERATION_DECISION_BREAK;
    }
    search->matches[search->count++] = node;
  }
  return UACPI_ITERATION_DECISION_CONTINUE;
}

enum acpi_wifi_state acpi_wifi_find_companion(const struct pci_device *device,
                                           struct companion_search *endpoints,
                                           enum acpi_wifi_state missing_root, bool log_aliases)
{
  const struct pci_device *ancestry[PCI_BUS_COUNT];
  size_t count = 0;
  for (const struct pci_device *at = device; at; at = at->parent) {
    if (count == PCI_BUS_COUNT) {
      return ACPI_WIFI_UNSUPPORTED;
    }
    ancestry[count++] = at;
  }
  /* Reading the companion's policy needs identity, not allocatable _CRS
   * apertures. A refused BAR-allocation snapshot must stay refused. */
  struct root_search roots = {
    .bus = ancestry[count - 1]->address.bus, .state = ACPI_WIFI_VALID,
  };
  uacpi_status status = uacpi_namespace_for_each_child(uacpi_namespace_root(), find_root,
      NULL, UACPI_OBJECT_DEVICE_BIT, UACPI_MAX_DEPTH_ANY, &roots);
  if (roots.state != ACPI_WIFI_VALID) {
    return roots.state;
  }
  if (status != UACPI_STATUS_OK) {
    return ACPI_WIFI_FAILED;
  }
  if (!roots.match) {
    return missing_root;
  }
  uacpi_namespace_node *parent = roots.match;
  struct companion_search search = {0};
  while (count) {
    const struct pci_device *at = ancestry[--count];
    search.address = ((uint64_t)at->address.device << 16) | at->address.function;
    search.count = 0;
    search.disabled = false;
    search.state = ACPI_WIFI_VALID;
    status = uacpi_namespace_for_each_child(parent, find_address, NULL,
        UACPI_OBJECT_ANY_BIT, 1, &search);
    if (search.state != ACPI_WIFI_VALID) {
      return search.state;
    }
    if (status != UACPI_STATUS_OK) {
      return ACPI_WIFI_FAILED;
    }
    if (!search.count) {
      return search.disabled ? ACPI_WIFI_DISABLED : ACPI_WIFI_ABSENT;
    }
    if (!count) {
      *endpoints = search;
      if (log_aliases && search.count > 1) {
        char names[ENDPOINT_ALIAS_CAPACITY * 5] = {0};
        for (size_t i = 0; i < search.count; ++i) {
          uacpi_object_name name = uacpi_namespace_node_name(search.matches[i]);
          memcpy(names + i * 5, name.text, 4);
          names[i * 5 + 4] = i + 1 < search.count ? ',' : '\0';
        }
        /* One bounded native-qualification diagnostic; no network identities. */
        uacpi_object_name root_name = uacpi_namespace_node_name(roots.match);
        char root_text[5] = {0};
        memcpy(root_text, root_name.text, 4);
        kinfo("wifi: ACPI root=%s SEG=0 BBN=%u endpoint=%u:%u.%u ADR=%llx aliases=%zu nodes=%s\n",
            root_text, roots.bus, device->address.bus, device->address.device,
            device->address.function, (unsigned long long)search.address, search.count, names);
      }
      return ACPI_WIFI_VALID;
    }
    if (search.count != 1) {
      return ACPI_WIFI_MALFORMED;
    }
    parent = search.matches[0];
  }
  return ACPI_WIFI_FAILED;
}

/* Resolve first: NOT_FOUND returned by a present AML method is a failed
 * evaluation, and must never become permission to use an absent-table default. */
enum acpi_wifi_state acpi_wifi_evaluate(uacpi_namespace_node *node, const char *method,
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

bool acpi_wifi_take_source(enum acpi_wifi_state *state, enum acpi_wifi_state incoming,
                         bool equal)
{
  if (incoming == ACPI_WIFI_ABSENT) {
    return false;
  }
  if (*state == ACPI_WIFI_ABSENT) {
    *state = incoming;
    return true;
  }
  if (*state != incoming || !equal) {
    *state = ACPI_WIFI_MALFORMED;
  }
  return false;
}
