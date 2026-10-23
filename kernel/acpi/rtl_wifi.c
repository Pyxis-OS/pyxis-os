/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright(c) 2019-2020, 2021-2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89/acpi.c, acpi.h and phy.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, selected BSD alternative.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
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
#include <kernel/acpi/rtl_wifi.h>
#include <kernel/boot/options.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include "host.h"
#include "wifi_namespace.h"

#define RTL_VENDOR 0x10ecu
#define RTL8922AE_DEVICE 0x8922u
#define FLATTEN_DEPTH 8u
#define FLATTEN_WORK 1024u
#define SAR_STATIC_HEADER 3u
#define SAR_DYNAMIC_HEADER 4u
#define SAR_TABLES 6u
#define RTAG_BYTES 31u
#define RTAG_DOMAIN_ETSI 1u
#define DSM_TAS_BYTES 10u
#define DSM_TAS_FUNCTION 5u
#define DSM_FUNCTIONS_VALID UINT64_C(1)
#define DSM_FUNCTION_TAS (UINT64_C(1) << DSM_TAS_FUNCTION)

/* Only the ACPI worker writes, before publication on the BSP. */
static struct acpi_rtl_wifi_policy captured;
static enum acpi_wifi_state snapshot_state;

static void source_states(enum acpi_wifi_state state)
{
  captured.wrds.data.state = state;
  captured.rwrd.data.state = state;
  captured.rwsi.state = state;
  captured.rwgs.state = state;
  captured.rtag.state = state;
  captured.dsm5.state = state;
}

static void publish(enum acpi_wifi_state state)
{
  uint64_t flags = cpu_save_interrupts();
  snapshot_state = state;
  cpu_restore_interrupts(flags);
}

void acpi_rtl_wifi_unavailable(void)
{
  source_states(ACPI_WIFI_FAILED);
  publish(ACPI_WIFI_FAILED);
}

/* Limits bound recursion, visited objects and owned output independently.
 * Unlike Linux's traversal, an integer outside the byte range is rejected. */
static enum acpi_wifi_state flatten(uacpi_object *object, struct acpi_rtl_wifi_data *data,
                                     unsigned depth, size_t *work)
{
  if (depth > FLATTEN_DEPTH || ++*work > FLATTEN_WORK) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  uint64_t integer;
  if (uacpi_object_get_integer(object, &integer) == UACPI_STATUS_OK) {
    if (integer > UINT8_MAX) {
      return ACPI_WIFI_MALFORMED;
    }
    if (data->length == ACPI_RTL_WIFI_BYTES) {
      return ACPI_WIFI_UNSUPPORTED;
    }
    data->bytes[data->length++] = integer;
    return ACPI_WIFI_VALID;
  }
  uacpi_data_view buffer;
  if (uacpi_object_get_buffer(object, &buffer) == UACPI_STATUS_OK) {
    if (!buffer.length) {
      return ACPI_WIFI_MALFORMED;
    }
    if (buffer.length > ACPI_RTL_WIFI_BYTES - data->length) {
      return ACPI_WIFI_UNSUPPORTED;
    }
    memcpy(data->bytes + data->length, buffer.bytes, buffer.length);
    data->length += buffer.length;
    return ACPI_WIFI_VALID;
  }
  uacpi_object_array package;
  if (uacpi_object_get_package(object, &package) != UACPI_STATUS_OK || !package.count) {
    return ACPI_WIFI_MALFORMED;
  }
  if (package.count > FLATTEN_WORK - *work) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  for (size_t i = 0; i < package.count; ++i) {
    enum acpi_wifi_state state = flatten(package.objects[i], data, depth + 1, work);
    if (state != ACPI_WIFI_VALID) {
      return state;
    }
  }
  return ACPI_WIFI_VALID;
}

static void capture_method(uacpi_namespace_node *node, const char *method,
                             struct acpi_rtl_wifi_data *data)
{
  uacpi_object *object = NULL;
  data->state = acpi_wifi_evaluate(node, method, NULL, &object);
  if (data->state == ACPI_WIFI_VALID) {
    size_t work = 0;
    data->state = flatten(object, data, 0, &work);
  }
  if (object) {
    uacpi_object_unref(object);
  }
  if (data->state != ACPI_WIFI_VALID) {
    /* Do not present partial traversal as an exact retained table. */
    data->length = 0;
    memset(data->bytes, 0, sizeof(data->bytes));
  }
}

static enum acpi_wifi_state validate_sar(struct acpi_rtl_wifi_sar *sar, bool dynamic)
{
  struct acpi_rtl_wifi_data *data = &sar->data;
  if (data->state != ACPI_WIFI_VALID) {
    return data->state;
  }
  sar->header_bytes = dynamic ? SAR_DYNAMIC_HEADER : SAR_STATIC_HEADER;
  if (data->length <= sar->header_bytes) {
    return ACPI_WIFI_MALFORMED;
  }
  sar->cid = data->bytes[0] | ((uint16_t)data->bytes[1] << 8);
  sar->revision = data->bytes[2];
  sar->tables = dynamic ? data->bytes[3] : 1;
  if (!sar->tables || sar->tables > SAR_TABLES) {
    return ACPI_WIFI_MALFORMED;
  }
  if ((sar->cid != ACPI_RTL_WIFI_CID_HP && sar->cid != ACPI_RTL_WIFI_CID_RT) ||
      (sar->revision != 1 && sar->revision != 2)) {
    return ACPI_WIFI_UNSUPPORTED;
  }
  sar->subbands = sar->revision == 1 ? 5 : 11;
  size_t bytes = data->length - sar->header_bytes;
  if (bytes % sar->tables) {
    return ACPI_WIFI_MALFORMED;
  }
  size_t table_bytes = bytes / sar->tables;
  if (table_bytes == 4u * sar->subbands) {
    sar->antennas = 4;
  } else if (sar->cid == ACPI_RTL_WIFI_CID_RT && table_bytes == 2u * sar->subbands) {
    sar->antennas = 2;
  } else {
    return ACPI_WIFI_MALFORMED;
  }
  sar->table_bytes = table_bytes;
  return ACPI_WIFI_VALID;
}

static void merge_data(struct acpi_rtl_wifi_data *target,
                         const struct acpi_rtl_wifi_data *source)
{
  if (acpi_wifi_take_source(&target->state, source->state,
      target->length == source->length &&
      !memcmp(target->bytes, source->bytes, source->length))) {
    *target = *source;
  }
}

static void merge_sar(struct acpi_rtl_wifi_sar *target,
                        const struct acpi_rtl_wifi_sar *source)
{
  if (acpi_wifi_take_source(&target->data.state, source->data.state,
      target->data.length == source->data.length &&
      !memcmp(target->data.bytes, source->data.bytes, source->data.length))) {
    *target = *source;
  }
}

static enum acpi_wifi_state evaluate_dsm(uacpi_namespace_node *node, unsigned function,
                                          uacpi_object **object,
                                          struct acpi_rtl_wifi_tas_diagnostic *diagnostic)
{
  static const uint8_t uuid[] = {
    0xe8, 0xc3, 0xa8, 0xd2, 0x69, 0x4b, 0x00, 0x4f,
    0x82, 0xbd, 0xfe, 0x86, 0x07, 0x80, 0x3a, 0xa7,
  };
  uacpi_object *arguments[] = {
    uacpi_object_create_buffer((uacpi_data_view){.const_bytes = uuid, .length = sizeof(uuid)}),
    uacpi_object_create_integer(0),
    uacpi_object_create_integer(function),
    uacpi_object_create_package((uacpi_object_array){0}),
  };
  enum acpi_wifi_state state = ACPI_WIFI_FAILED;
  diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_ALLOCATION;
  if (arguments[0] && arguments[1] && arguments[2] && arguments[3]) {
    /* Match acpi_wifi_evaluate's lookup-before-evaluation distinction. */
    uacpi_namespace_node *source;
    uacpi_status status = uacpi_namespace_node_find(node, "_DSM", &source);
    diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_LOOKUP;
    if (status == UACPI_STATUS_NOT_FOUND) {
      state = ACPI_WIFI_ABSENT;
    } else if (status == UACPI_STATUS_OK) {
      diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_EVALUATION;
      if (uacpi_eval(source, NULL,
          &(uacpi_object_array){.objects = arguments, .count = 4}, object) == UACPI_STATUS_OK) {
        state = *object ? ACPI_WIFI_VALID : ACPI_WIFI_MALFORMED;
        diagnostic->reason = *object ? ACPI_RTL_WIFI_TAS_REASON_NONE :
            ACPI_RTL_WIFI_TAS_REASON_TYPE;
      }
    }
  }
  for (size_t i = 0; i < 4; ++i) {
    if (arguments[i]) {
      uacpi_object_unref(arguments[i]);
    }
  }
  return state;
}

static bool tas_buffer(uacpi_object *object, uacpi_data_view *buffer,
                       struct acpi_rtl_wifi_tas_diagnostic *diagnostic)
{
  switch (uacpi_object_get_type(object)) {
  case UACPI_OBJECT_INTEGER:
    diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_INTEGER;
    break;
  case UACPI_OBJECT_BUFFER:
    diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_BUFFER;
    break;
  case UACPI_OBJECT_PACKAGE:
    diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_PACKAGE;
    break;
  case UACPI_OBJECT_STRING:
    diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_STRING;
    break;
  default:
    diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_OTHER;
    break;
  }
  diagnostic->object_observed = true;
  if (uacpi_object_get_buffer(object, buffer) != UACPI_STATUS_OK) {
    diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_TYPE;
    return false;
  }
  diagnostic->length = buffer->length;
  diagnostic->length_observed = true;
  return true;
}

static void capture_dsm5(uacpi_namespace_node *node, struct acpi_rtl_wifi_policy *policy)
{
  struct acpi_rtl_wifi_data *data = &policy->dsm5;
  struct acpi_rtl_wifi_tas_diagnostic *diagnostic = &policy->tas;
  diagnostic->stage = ACPI_RTL_WIFI_TAS_STAGE_QUERY_0;
  uacpi_object *object = NULL;
  data->state = evaluate_dsm(node, 0, &object, diagnostic);
  if (data->state == ACPI_WIFI_VALID) {
    uacpi_data_view buffer;
    if (!tas_buffer(object, &buffer, diagnostic)) {
      data->state = ACPI_WIFI_MALFORMED;
    } else if (!buffer.length) {
      data->state = ACPI_WIFI_MALFORMED;
      diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_LENGTH;
    } else if (buffer.length > sizeof(policy->dsm_supported)) {
      data->state = ACPI_WIFI_UNSUPPORTED;
      diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_LENGTH;
    } else {
      for (size_t i = 0; i < buffer.length; ++i) {
        policy->dsm_supported |= (uint64_t)buffer.bytes[i] << (8 * i);
      }
      diagnostic->supported = policy->dsm_supported;
      diagnostic->support_observed = true;
      if (!(policy->dsm_supported & DSM_FUNCTIONS_VALID)) {
        data->state = policy->dsm_supported ? ACPI_WIFI_MALFORMED : ACPI_WIFI_ABSENT;
        diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_SUPPORT_MASK;
      } else if (!(policy->dsm_supported & DSM_FUNCTION_TAS)) {
        data->state = ACPI_WIFI_ABSENT;
        diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_SUPPORT_MASK;
      }
    }
  }
  if (object) {
    uacpi_object_unref(object);
    object = NULL;
  }
  if (data->state != ACPI_WIFI_VALID) {
    return;
  }
  diagnostic->stage = ACPI_RTL_WIFI_TAS_STAGE_FUNCTION_5;
  diagnostic->object_type = ACPI_RTL_WIFI_TAS_OBJECT_UNOBSERVED;
  diagnostic->object_observed = false;
  diagnostic->length = 0;
  diagnostic->length_observed = false;
  data->state = evaluate_dsm(node, DSM_TAS_FUNCTION, &object, diagnostic);
  if (data->state == ACPI_WIFI_ABSENT) {
    data->state = ACPI_WIFI_FAILED;
  }
  if (data->state == ACPI_WIFI_VALID) {
    uacpi_data_view buffer;
    if (!tas_buffer(object, &buffer, diagnostic)) {
      data->state = ACPI_WIFI_MALFORMED;
    } else if (buffer.length < DSM_TAS_BYTES) {
      data->state = ACPI_WIFI_MALFORMED;
      diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_LENGTH;
    } else if (buffer.length > ACPI_RTL_WIFI_BYTES) {
      data->state = ACPI_WIFI_UNSUPPORTED;
      diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_LENGTH;
    } else {
      data->length = buffer.length;
      memcpy(data->bytes, buffer.bytes, buffer.length);
      static const uint8_t signature[] = {'R', 'T', 'K', DSM_TAS_FUNCTION};
      if (memcmp(data->bytes, signature, sizeof(signature))) {
        data->state = ACPI_WIFI_MALFORMED;
        diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_SIGNATURE;
      } else if (data->bytes[4] != 0) {
        data->state = ACPI_WIFI_UNSUPPORTED;
        diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_REVISION;
      } else if (data->bytes[5] > 1) {
        data->state = ACPI_WIFI_UNSUPPORTED;
        diagnostic->reason = ACPI_RTL_WIFI_TAS_REASON_ENABLE;
      }
    }
  }
  if (object) {
    uacpi_object_unref(object);
  }
}

static void merge_tas(struct acpi_rtl_wifi_policy *target,
                      const struct acpi_rtl_wifi_policy *source)
{
  struct acpi_rtl_wifi_data *data = &target->dsm5;
  const struct acpi_rtl_wifi_data *incoming = &source->dsm5;
  if (incoming->state == ACPI_WIFI_ABSENT) {
    if (data->state == ACPI_WIFI_ABSENT && target->tas.stage == ACPI_RTL_WIFI_TAS_STAGE_NONE) {
      target->tas = source->tas;
    }
    return;
  }
  bool equal = data->length == incoming->length &&
      !memcmp(data->bytes, incoming->bytes, incoming->length);
  const struct acpi_rtl_wifi_tas_diagnostic *previous = &target->tas;
  const struct acpi_rtl_wifi_tas_diagnostic *next = &source->tas;
  bool same_rejection = previous->stage == next->stage && previous->reason == next->reason &&
      previous->object_type == next->object_type && previous->length == next->length &&
      previous->supported == next->supported &&
      previous->object_observed == next->object_observed &&
      previous->length_observed == next->length_observed &&
      previous->support_observed == next->support_observed;
  bool conflict = data->state != ACPI_WIFI_ABSENT &&
      (data->state != incoming->state || !equal ||
       (data->state != ACPI_WIFI_VALID && !same_rejection));
  if (acpi_wifi_take_source(&data->state, incoming->state, equal)) {
    *data = *incoming;
    target->tas = source->tas;
  } else if (conflict) {
    if (target->tas.reason == ACPI_RTL_WIFI_TAS_REASON_NONE) {
      if (source->tas.reason != ACPI_RTL_WIFI_TAS_REASON_NONE) {
        target->tas = source->tas;
      } else {
        target->tas.stage = ACPI_RTL_WIFI_TAS_STAGE_ALIAS;
        target->tas.reason = ACPI_RTL_WIFI_TAS_REASON_ALIAS;
      }
    }
    target->tas.alias_conflict = true;
  }
}

static void validate_indicator(void)
{
  if (captured.rwsi.state != ACPI_WIFI_VALID) {
    return;
  }
  if (captured.rwrd.data.state != ACPI_WIFI_VALID) {
    captured.rwsi.state = ACPI_WIFI_UNSUPPORTED;
    return;
  }
  size_t fields = captured.rwrd.cid == ACPI_RTL_WIFI_CID_HP ? 4 : 1;
  if (captured.rwsi.length != fields) {
    captured.rwsi.state = ACPI_WIFI_MALFORMED;
    return;
  }
  for (size_t i = 0; i < fields; ++i) {
    if (!captured.rwsi.bytes[i] || captured.rwsi.bytes[i] > captured.rwrd.tables) {
      captured.rwsi.state = ACPI_WIFI_MALFORMED;
      return;
    }
  }
}

static void validate_geo(void)
{
  if (captured.rwgs.state != ACPI_WIFI_VALID) {
    return;
  }
  const struct acpi_rtl_wifi_sar *sar = &captured.wrds;
  if (sar->data.state != ACPI_WIFI_VALID) {
    sar = &captured.rwrd;
  }
  if (sar->data.state != ACPI_WIFI_VALID) {
    captured.rwgs.state = ACPI_WIFI_UNSUPPORTED;
    return;
  }
  captured.geo_cid = sar->cid;
  captured.geo_revision = sar->revision;
  captured.geo_regions = sar->cid == ACPI_RTL_WIFI_CID_HP ? 3 : 6;
  captured.geo_bands = sar->revision == 1 ? 2 : 3;
  size_t value_bytes = sar->cid == ACPI_RTL_WIFI_CID_HP ? 5 : 2;
  if (captured.rwgs.length != captured.geo_regions * captured.geo_bands * value_bytes) {
    captured.rwgs.state = ACPI_WIFI_MALFORMED;
  }
}

static void validate_rtag(struct acpi_rtl_wifi_data *data)
{
  if (data->state != ACPI_WIFI_VALID) {
    return;
  }
  if (data->length != RTAG_BYTES) {
    data->state = ACPI_WIFI_MALFORMED;
    return;
  }
  uint32_t domain = 0;
  for (size_t i = 0; i < 4; ++i) {
    domain |= (uint32_t)data->bytes[5 + i] << (8 * i);
  }
  /* The reference does not interpret the four tag bytes. Preserve them. */
  if (data->bytes[4] != 0 || (domain & ~RTAG_DOMAIN_ETSI)) {
    data->state = ACPI_WIFI_UNSUPPORTED;
  }
}

void acpi_rtl_wifi_start(void)
{
  acpi_require_worker();
  KASSERT(snapshot_state == ACPI_WIFI_PENDING);
  if (!boot_options_get()->wifi_rtl8922ae) {
    source_states(ACPI_WIFI_DISABLED);
    publish(ACPI_WIFI_DISABLED);
    return;
  }
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(RTL_VENDOR, RTL8922AE_DEVICE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    enum acpi_wifi_state state = selection == PCI_SELECTION_ABSENT ? ACPI_WIFI_ABSENT :
        selection == PCI_SELECTION_AMBIGUOUS ? ACPI_WIFI_UNSUPPORTED : ACPI_WIFI_FAILED;
    source_states(state);
    publish(state);
    return;
  }
  captured.address = device->address;
  struct companion_search endpoints = {0};
  enum acpi_wifi_state state = acpi_wifi_find_companion(device, &endpoints, ACPI_WIFI_ABSENT, false);
  if (state != ACPI_WIFI_VALID) {
    source_states(state);
    publish(state);
    return;
  }
  source_states(ACPI_WIFI_ABSENT);
  for (size_t i = 0; i < endpoints.count; ++i) {
    struct acpi_rtl_wifi_policy source = {0};
    uacpi_namespace_node *node = endpoints.matches[i];
    capture_method(node, "WRDS", &source.wrds.data);
    source.wrds.data.state = validate_sar(&source.wrds, false);
    capture_method(node, "RWRD", &source.rwrd.data);
    source.rwrd.data.state = validate_sar(&source.rwrd, true);
    capture_method(node, "RWSI", &source.rwsi);
    capture_method(node, "RWGS", &source.rwgs);
    capture_method(node, "RTAG", &source.rtag);
    validate_rtag(&source.rtag);
    capture_dsm5(node, &source);
    captured.dsm_supported |= source.dsm_supported;
    merge_sar(&captured.wrds, &source.wrds);
    merge_sar(&captured.rwrd, &source.rwrd);
    merge_data(&captured.rwsi, &source.rwsi);
    merge_data(&captured.rwgs, &source.rwgs);
    merge_data(&captured.rtag, &source.rtag);
    merge_tas(&captured, &source);
  }
  validate_indicator();
  validate_geo();
  publish(ACPI_WIFI_VALID);
}

enum acpi_wifi_state acpi_rtl_wifi_snapshot(struct acpi_rtl_wifi_policy *policy)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (snapshot_state != ACPI_WIFI_PENDING) {
    *policy = captured;
  }
  return snapshot_state;
}
