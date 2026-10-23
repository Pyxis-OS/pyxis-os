#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/acpi/rtl_wifi.h>
#include <kernel/boot/options.h>
#include <kernel/initrd.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "rtl8922ae.h"
#include "rtl8922ae_firmware.h"
#include "rtl8922ae_hw.h"
#include "rtl8922ae_sar.h"
#include "rtl8922ae_transport.h"
#include "regdb.h"

#define RTL8922AE_VENDOR 0x10ecu
#define RTL8922AE_DEVICE 0x8922u
#define RTL8922AE_START_MS 30000u
#define RTL8922AE_STOP_MS 10000u

/* The BSP owns preparation and the sole worker. Status publication uses IF=0;
 * archive bytes, plans, PCI context and DMA backing survive the worker. */
static struct {
  struct wifi_status_reply status;
  struct initrd_file asset;
  struct rtl8922ae_firmware_info inspection;
  struct rtl8922ae_firmware_plan plan;
  struct rtl8922ae_board board;
  struct rtl8922ae_firmware_tables tables;
  struct acpi_rtl_wifi_policy platform;
  bool prepared, started;
  uint8_t cut;
} preparation;

static uint32_t input_state(enum acpi_wifi_state state)
{
  switch (state) {
  case ACPI_WIFI_PENDING: return WIFI_INPUT_PENDING;
  case ACPI_WIFI_ABSENT: return WIFI_INPUT_ABSENT;
  case ACPI_WIFI_DISABLED: return WIFI_INPUT_DISABLED;
  case ACPI_WIFI_VALID: return WIFI_INPUT_VALID;
  case ACPI_WIFI_UNSUPPORTED: return WIFI_INPUT_UNSUPPORTED;
  case ACPI_WIFI_MALFORMED: return WIFI_INPUT_MALFORMED;
  case ACPI_WIFI_FAILED: return WIFI_INPUT_FAILED;
  }
  return WIFI_INPUT_FAILED;
}

static void publish_tas(struct wifi_tas_diagnostic *out,
    const struct acpi_rtl_wifi_tas_diagnostic *tas)
{
  switch (tas->stage) {
  case ACPI_RTL_WIFI_TAS_STAGE_NONE: out->stage = WIFI_TAS_STAGE_NONE; break;
  case ACPI_RTL_WIFI_TAS_STAGE_QUERY_0: out->stage = WIFI_TAS_STAGE_QUERY_0; break;
  case ACPI_RTL_WIFI_TAS_STAGE_FUNCTION_5: out->stage = WIFI_TAS_STAGE_FUNCTION_5; break;
  case ACPI_RTL_WIFI_TAS_STAGE_ALIAS: out->stage = WIFI_TAS_STAGE_ALIAS; break;
  }
  switch (tas->reason) {
  case ACPI_RTL_WIFI_TAS_REASON_NONE: out->reason = WIFI_TAS_REASON_NONE; break;
  case ACPI_RTL_WIFI_TAS_REASON_LOOKUP: out->reason = WIFI_TAS_REASON_LOOKUP; break;
  case ACPI_RTL_WIFI_TAS_REASON_EVALUATION: out->reason = WIFI_TAS_REASON_EVALUATION; break;
  case ACPI_RTL_WIFI_TAS_REASON_ALLOCATION: out->reason = WIFI_TAS_REASON_ALLOCATION; break;
  case ACPI_RTL_WIFI_TAS_REASON_TYPE: out->reason = WIFI_TAS_REASON_TYPE; break;
  case ACPI_RTL_WIFI_TAS_REASON_LENGTH: out->reason = WIFI_TAS_REASON_LENGTH; break;
  case ACPI_RTL_WIFI_TAS_REASON_SUPPORT_MASK: out->reason = WIFI_TAS_REASON_SUPPORT_MASK; break;
  case ACPI_RTL_WIFI_TAS_REASON_SIGNATURE: out->reason = WIFI_TAS_REASON_SIGNATURE; break;
  case ACPI_RTL_WIFI_TAS_REASON_REVISION: out->reason = WIFI_TAS_REASON_REVISION; break;
  case ACPI_RTL_WIFI_TAS_REASON_ENABLE: out->reason = WIFI_TAS_REASON_ENABLE; break;
  case ACPI_RTL_WIFI_TAS_REASON_ALIAS: out->reason = WIFI_TAS_REASON_ALIAS; break;
  }
  switch (tas->object_type) {
  case ACPI_RTL_WIFI_TAS_OBJECT_UNOBSERVED: out->object_type = WIFI_TAS_OBJECT_UNOBSERVED; break;
  case ACPI_RTL_WIFI_TAS_OBJECT_INTEGER: out->object_type = WIFI_TAS_OBJECT_INTEGER; break;
  case ACPI_RTL_WIFI_TAS_OBJECT_BUFFER: out->object_type = WIFI_TAS_OBJECT_BUFFER; break;
  case ACPI_RTL_WIFI_TAS_OBJECT_PACKAGE: out->object_type = WIFI_TAS_OBJECT_PACKAGE; break;
  case ACPI_RTL_WIFI_TAS_OBJECT_STRING: out->object_type = WIFI_TAS_OBJECT_STRING; break;
  case ACPI_RTL_WIFI_TAS_OBJECT_OTHER: out->object_type = WIFI_TAS_OBJECT_OTHER; break;
  }
  if (tas->object_observed) out->flags |= WIFI_TAS_OBJECT_VALID;
  if (tas->length_observed) out->flags |= WIFI_TAS_LENGTH_VALID;
  if (tas->support_observed) out->flags |= WIFI_TAS_SUPPORT_VALID;
  if (tas->alias_conflict) out->flags |= WIFI_TAS_ALIAS_CONFLICT;
  out->length = tas->length;
  out->supported = tas->supported;
}

static struct wifi_sar_source sar_source(const struct rtl8922ae_sar_source *source)
{
  return (struct wifi_sar_source){source->cid, source->revision, source->antennas,
      source->subbands, source->profiles};
}

static void publish_sar(struct wifi_sar_inputs *out,
    const struct acpi_rtl_wifi_policy *platform, enum acpi_wifi_state capture_state)
{
  struct rtl8922ae_sar_envelope sar = rtl8922ae_sar_envelope(platform, capture_state);
  out->state = input_state(sar.state);
  if (sar.source_flags & RTL8922AE_SAR_SOURCE_WRDS) out->sources |= WIFI_SAR_SOURCE_WRDS;
  if (sar.source_flags & RTL8922AE_SAR_SOURCE_RWRD) out->sources |= WIFI_SAR_SOURCE_RWRD;
  out->wrds = sar_source(&sar.wrds);
  out->rwrd = sar_source(&sar.rwrd);
  for (size_t path = 0; path < RTL8922AE_SAR_PATH_COUNT; ++path) {
    out->limits_1tx_eighth_dbm[path] = sar.limits_1tx_eighth_dbm[path];
    out->limits_2tx_eighth_dbm[path] = sar.limits_2tx_eighth_dbm[path];
  }
}

static void publish_rules(const struct wifi_regdb_profile *profile, uint32_t channels)
{
  struct wifi_radio_inputs *radio = &preparation.status.radio;
  radio->rule_channels = profile->channels & channels;
  memset(radio->channel, 0, sizeof(radio->channel));
  for (size_t i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    if (radio->rule_channels & (UINT32_C(1) << i)) {
      radio->channel[i] = (struct wifi_channel_rule){
        .max_eirp_mbm = profile->channel[i].max_eirp_mbm,
        .flags = profile->channel[i].flags,
      };
    }
  }
}

static void set_detail(const char *detail)
{
  size_t i = 0;
  while (i + 1 < sizeof(preparation.status.detail) && detail[i]) {
    preparation.status.detail[i] = detail[i];
    ++i;
  }
  preparation.status.detail[i] = '\0';
}

static void failed(const char *detail)
{
  preparation.status.flags |= WIFI_FAILED;
  set_detail(detail);
}

static bool download(uint64_t deadline, const char **detail)
{
  uint8_t security[4];
  if (!rtl8922ae_hw_cut(&preparation.cut, detail)) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  preparation.status.boot.live_cut = preparation.cut;
  preparation.status.boot.valid |= WIFI_BOOT_CUT_VALID;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_hw_power_on(preparation.cut, deadline, detail) ||
      !rtl8922ae_hw_security(security, preparation.cut, deadline, detail)) {
    return false;
  }
  enum rtl8922ae_firmware_plan_result result = rtl8922ae_firmware_plan(
      preparation.asset.data, preparation.asset.size, preparation.cut, security, &preparation.plan);
  memset(security, 0, sizeof(security));
  if (result != RTL8922AE_FIRMWARE_PLAN_OK) {
    switch (result) {
    case RTL8922AE_FIRMWARE_PLAN_SECURITY_INVALID:
      *detail = "RTL8922AE programmed security selector invalid";
      break;
    case RTL8922AE_FIRMWARE_PLAN_NO_IMAGE:
      *detail = "RTL8922AE normal firmware image for live cut unavailable";
      break;
    case RTL8922AE_FIRMWARE_PLAN_NO_SECURITY:
      *detail = "RTL8922AE matching firmware security section unavailable";
      break;
    case RTL8922AE_FIRMWARE_PLAN_BLACKLISTED:
      *detail = "RTL8922AE firmware key is blacklisted for this device";
      break;
    default:
      *detail = "RTL8922AE live firmware/security profile unsupported or malformed";
      break;
    }
    return false;
  }
  flags = cpu_save_interrupts();
  preparation.status.boot.secure_boot = preparation.plan.security.secure_boot;
  memcpy(preparation.status.boot.firmware_version, preparation.plan.version,
      sizeof(preparation.status.boot.firmware_version));
  preparation.status.boot.valid |= WIFI_BOOT_SECURITY_VALID | WIFI_BOOT_IMAGE_VALID;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_hw_download_prepare(deadline, detail) ||
      !rtl8922ae_transport_begin(preparation.cut, deadline, detail) ||
      !rtl8922ae_transport_download(preparation.asset.data, preparation.asset.size,
          &preparation.plan, deadline, detail)) {
    return false;
  }
  if (!rtl8922ae_hw_board(&preparation.board, preparation.cut, deadline, detail)) {
    return false;
  }
  flags = cpu_save_interrupts();
  preparation.status.boot.rfe = preparation.board.rfe;
  preparation.status.boot.xtal = preparation.board.xtal;
  preparation.status.boot.tx_nss = preparation.board.tx_nss;
  preparation.status.boot.rx_nss = preparation.board.rx_nss;
  preparation.status.boot.tx_antennas = preparation.board.tx_antennas;
  preparation.status.boot.rx_antennas = preparation.board.rx_antennas;
  memcpy(preparation.status.boot.board_country, preparation.board.country, 2);
  preparation.status.boot.valid |= WIFI_BOOT_BOARD_VALID;
  if (preparation.board.rfkill) {
    preparation.status.flags |= WIFI_RFKILL;
  }
  cpu_restore_interrupts(flags);
  enum rtl8922ae_tables_result tables = rtl8922ae_firmware_tables(
      preparation.asset.data, preparation.asset.size, preparation.cut,
      preparation.board.rfe, &preparation.tables);
  flags = cpu_save_interrupts();
  switch (tables) {
  case RTL8922AE_TABLES_OK: preparation.status.radio.tables_state = WIFI_INPUT_VALID; break;
  case RTL8922AE_TABLES_MISSING: preparation.status.radio.tables_state = WIFI_INPUT_ABSENT; break;
  case RTL8922AE_TABLES_MALFORMED: preparation.status.radio.tables_state = WIFI_INPUT_MALFORMED; break;
  case RTL8922AE_TABLES_UNSUPPORTED: preparation.status.radio.tables_state = WIFI_INPUT_UNSUPPORTED; break;
  }
  if (preparation.tables.default_power_mask & RTL8922AE_TABLES_POWER_2G_MASK) {
    preparation.status.radio.tables_flags |= WIFI_TABLE_POWER_DEFAULT;
  }
  cpu_restore_interrupts(flags);
  return true;
}

static void rtl8922ae_worker(void *argument)
{
  (void)argument;
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  preparation.status.boot.state |= WIFI_BOOT_ATTEMPTED;
  cpu_restore_interrupts(flags);
  const char *detail = "RTL8922AE firmware startup failed";
  bool ready = download(task_deadline_after_ms(RTL8922AE_START_MS), &detail);
  const char *stop_detail = "RTL8922AE stop unconfirmed; ownership retained";
  bool stopped = rtl8922ae_transport_stop(task_deadline_after_ms(RTL8922AE_STOP_MS), &stop_detail);
  flags = cpu_save_interrupts();
  if (!ready) {
    failed(detail);
  } else if (!stopped) {
    failed(stop_detail);
  } else {
    set_detail("RTL8922AE firmware ready; MAC off/reset confirmed; input observations only; RF unqualified");
  }
  cpu_restore_interrupts(flags);
}

void rtl8922ae_prepare(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  preparation.status.flags = WIFI_ENABLED;
  preparation.status.radio.kind = WIFI_RADIO_INPUTS_RTL8922AE;
  preparation.status.policy_channels = WIFI_SCAN_CHANNELS;
  memcpy(preparation.status.country, "00", 2);
  struct wifi_regdb_profile world;
  KASSERT(wifi_regdb_profile(preparation.status.country, &world));
  publish_rules(&world, WIFI_SCAN_CHANNELS);
  if (boot_options_get()->wifi_ax200) {
    failed("Select only one Wi-Fi backend; both radios left untouched");
    return;
  }
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(RTL8922AE_VENDOR, RTL8922AE_DEVICE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    if (selection != PCI_SELECTION_ABSENT) {
      preparation.status.flags |= WIFI_FAILED;
    }
    set_detail(selection == PCI_SELECTION_ABSENT ? "RTL8922AE absent" :
        "RTL8922AE inventory unavailable or ambiguous");
    return;
  }
  preparation.status.flags |= WIFI_PRESENT;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    if (pci_device_at(i) == device) {
      preparation.status.controller_id = (uint32_t)i + 1;
      break;
    }
  }
  preparation.status.pci_bus = device->address.bus;
  preparation.status.pci_device = device->address.device;
  preparation.status.pci_function = device->address.function;
  if (initrd_lookup("firmware/rtl8922ae/rtw8922a_fw-4.bin", &preparation.asset) != INITRD_OK ||
      !rtl8922ae_firmware_inspect(preparation.asset.data, preparation.asset.size,
          &preparation.inspection)) {
    failed("RTL8922AE pinned firmware unavailable or malformed; device untouched");
    return;
  }
  const char *detail;
  if (!rtl8922ae_transport_prepare(boot, device, preparation.asset.size, &detail)) {
    failed(detail);
    return;
  }
  preparation.prepared = true;
  preparation.status.flags |= WIFI_PREPARED;
  set_detail("RTL8922AE DMA prepared; firmware startup pending");
}

void rtl8922ae_start(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled() && !preparation.started);
  preparation.started = true;
  if (preparation.prepared && kernel_task_create(rtl8922ae_worker, NULL) != MM_OK) {
    failed("RTL8922AE worker unavailable; no driver DMA or firmware started");
  }
}

void rtl8922ae_interrupt(void)
{
  rtl8922ae_transport_interrupt();
}

void rtl8922ae_status(struct wifi_status_reply *status)
{
  *status = preparation.status;
  rtl8922ae_transport_status(status);
  enum acpi_wifi_state state = acpi_rtl_wifi_snapshot(&preparation.platform);
  struct wifi_radio_inputs *radio = &status->radio;
  const struct acpi_rtl_wifi_policy *platform = &preparation.platform;
  radio->acpi_state = input_state(state);
  radio->static_sar_state = input_state(platform->wrds.data.state);
  radio->dynamic_sar_state = input_state(platform->rwrd.data.state);
  radio->sar_selector_state = input_state(platform->rwsi.state);
  radio->geographic_sar_state = input_state(platform->rwgs.state);
  radio->antenna_gain_state = input_state(platform->rtag.state);
  radio->tas_state = input_state(platform->dsm5.state);
  publish_tas(&radio->tas, &platform->tas);
  publish_sar(&radio->sar, platform, state);
}

enum call_status rtl8922ae_policy(const struct wifi_policy_request *policy)
{
  if (!policy->channels || (policy->channels & ~WIFI_SCAN_CHANNELS) ||
      policy->reserved[0] || policy->reserved[1]) {
    return CALL_BAD_REQUEST;
  }
  struct wifi_regdb_profile profile;
  if (!wifi_regdb_profile(policy->country, &profile)) {
    return CALL_BAD_REQUEST;
  }
  if (!(preparation.status.flags & WIFI_PRESENT)) {
    return CALL_UNAVAILABLE;
  }
  if (preparation.status.flags & WIFI_POLICY_INSTALLED) {
    return CALL_ALREADY_EXISTS;
  }
  memcpy(preparation.status.country, policy->country, 2);
  preparation.status.policy_channels = policy->channels;
  preparation.status.flags |= WIFI_POLICY_INSTALLED;
  publish_rules(&profile, policy->channels);
  return CALL_OK;
}
