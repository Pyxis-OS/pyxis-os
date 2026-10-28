#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/acpi/rtl_wifi.h>
#include <kernel/boot/options.h>
#include <kernel/initrd.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/wifi.h>
#include "rtl8922ae.h"
#include "rtl8922ae_firmware.h"
#include "rtl8922ae_discovery.h"
#include "rtl8922ae_scan.h"
#include "rtl8922ae_hw.h"
#include "rtl8922ae_sar.h"
#include "rtl8922ae_io.h"
#include "rtl8922ae_mac.h"
#include "rtl8922ae_phy.h"
#include "rtl8922ae_power.h"
#include "rtl8922ae_rfk.h"
#include "rtl8922ae_transport.h"
#include "regdb.h"

#define RTL8922AE_VENDOR 0x10ecu
#define RTL8922AE_DEVICE 0x8922u
#define RTL8922AE_SCAN_MS 15000u
#define RTL8922AE_RFKILL_NS UINT64_C(10000000)
#define RTL8922AE_STOP_MS 10000u

/* The BSP owns preparation and the sole worker. Status publication uses IF=0;
 * archive bytes, plans, PCI context and DMA backing survive the worker. */
static struct {
  struct wifi_status_full_reply status;
  struct initrd_file asset;
  struct rtl8922ae_firmware_info inspection;
  struct rtl8922ae_firmware_plan plan, bbmcu;
  struct rtl8922ae_board board;
  struct rtl8922ae_firmware_tables tables;
  struct acpi_rtl_wifi_policy platform;
  struct rtl8922ae_sar_envelope sar;
  struct wifi_regdb_profile rules;
  struct rtl8922ae_power_image power;
  struct rtl8922ae_rfk_commands rfk[2];
  struct wifi_scan_work *pending;
  struct task_wait *worker_wait;
  struct rtl8922ae_discovery discovery;
  uint64_t notifications;
  enum call_status abort_status;
  uint64_t next_rfkill;
  uint8_t channel;
  bool prepared, started, attempted, stopping, rfkill_live, live, worker_active;
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
  struct wifi_radio_inputs *radio = &preparation.status.base.radio;
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
  while (i + 1 < sizeof(preparation.status.base.detail) && detail[i]) {
    preparation.status.base.detail[i] = detail[i];
    ++i;
  }
  preparation.status.base.detail[i] = '\0';
}

static void failed(const char *detail)
{
  preparation.status.base.flags |= WIFI_FAILED;
  set_detail(detail);
}

static void phase(uint32_t value)
{
  uint64_t flags = cpu_save_interrupts();
  preparation.status.phy.phase = value;
  cpu_restore_interrupts(flags);
}

static bool rfkill_current(const char **detail)
{
  bool blocked = rtl8922ae_hw_rfkill();
  uint64_t flags = cpu_save_interrupts();
  ++preparation.status.phy.rfkill_checks;
  preparation.status.phy.state |= WIFI_PHY_RFKILL_VALID;
  if (blocked) preparation.status.base.flags |= WIFI_RFKILL;
  else preparation.status.base.flags &= ~WIFI_RFKILL;
  cpu_restore_interrupts(flags);
  if (blocked) *detail = "RTL8922AE current rfkill blocked";
  return !blocked;
}

bool rtl8922ae_operation_continue(void)
{
  if (preparation.stopping) return true;
  if (!preparation.pending || preparation.abort_status != CALL_OK) return false;
  uint64_t flags = cpu_save_interrupts();
  enum call_status reason = wifi_scan_stop_reason(preparation.pending);
  cpu_restore_interrupts(flags);
  if (reason == CALL_OK && task_deadline_expired(preparation.pending->deadline_ns)) {
    reason = CALL_TIMED_OUT;
  }
  if (reason != CALL_OK) {
    preparation.abort_status = reason;
    return false;
  }
  uint64_t now = arch_monotonic_ns();
  if (preparation.rfkill_live && now >= preparation.next_rfkill) {
    const char *detail = NULL;
    if (!rfkill_current(&detail)) {
      preparation.abort_status = CALL_UNAVAILABLE;
      flags = cpu_save_interrupts();
      set_detail(detail);
      cpu_restore_interrupts(flags);
      return false;
    }
    preparation.next_rfkill = now > UINT64_MAX - RTL8922AE_RFKILL_NS ?
        UINT64_MAX : now + RTL8922AE_RFKILL_NS;
  }
  return true;
}

static bool download(uint64_t deadline, const char **detail)
{
  uint8_t security[4];
  if (!rtl8922ae_operation_continue()) return false;
  preparation.cut = rtl8922ae_hw_cut();
  uint64_t flags = cpu_save_interrupts();
  preparation.status.base.boot.live_cut = preparation.cut;
  preparation.status.base.boot.valid |= WIFI_BOOT_CUT_VALID;
  cpu_restore_interrupts(flags);
  /* AFE precedes MAC power-on, before the live RFE can be read. Inspect only
   * that independent element here, without inventing a board selection. */
  enum rtl8922ae_tables_result afe = rtl8922ae_firmware_afe_table(
      preparation.asset.data, preparation.asset.size,
      &preparation.tables.table[RTL8922AE_ELEMENT_AFE]);
  if (afe != RTL8922AE_TABLES_OK && afe != RTL8922AE_TABLES_MISSING) {
    *detail = "RTL8922AE AFE table malformed or unsupported";
    return false;
  }
  if (afe == RTL8922AE_TABLES_OK) {
    preparation.tables.observed_mask |= RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_AFE);
  }
  if (!rtl8922ae_phy_afe(preparation.asset.data, preparation.asset.size,
          &preparation.tables, deadline, detail) ||
      !rtl8922ae_hw_power_on(preparation.cut, deadline, detail)) return false;
  rtl8922ae_hw_rfkill_prepare();
  preparation.rfkill_live = true;
  preparation.next_rfkill = 0;
  if (!rtl8922ae_operation_continue() ||
      !rtl8922ae_hw_security(security, preparation.cut, deadline, detail)) return false;
  enum rtl8922ae_firmware_plan_result result = rtl8922ae_firmware_plan(
      preparation.asset.data, preparation.asset.size, preparation.cut, security, &preparation.plan);
  enum rtl8922ae_firmware_plan_result bbmcu = rtl8922ae_firmware_plan_bbmcu0(
      preparation.asset.data, preparation.asset.size, preparation.cut, security, &preparation.bbmcu);
  memset(security, 0, sizeof(security));
  if (result != RTL8922AE_FIRMWARE_PLAN_OK || bbmcu != RTL8922AE_FIRMWARE_PLAN_OK) {
    *detail = result != RTL8922AE_FIRMWARE_PLAN_OK ?
        "RTL8922AE live WCPU firmware/security plan refused" :
        "RTL8922AE live BBMCU0 firmware/security plan refused";
    return false;
  }
  flags = cpu_save_interrupts();
  preparation.status.base.boot.secure_boot = preparation.plan.security.secure_boot;
  memcpy(preparation.status.base.boot.firmware_version, preparation.plan.version,
      sizeof(preparation.status.base.boot.firmware_version));
  preparation.status.base.boot.valid |= WIFI_BOOT_SECURITY_VALID | WIFI_BOOT_IMAGE_VALID;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_phy_preinit(deadline, detail) ||
      !rtl8922ae_hw_download_prepare(deadline, detail) ||
      !rtl8922ae_transport_begin(preparation.cut, deadline, detail) ||
      !rtl8922ae_transport_download_onephy(preparation.asset.data, preparation.asset.size,
          &preparation.plan, &preparation.bbmcu, deadline, detail)) return false;
  flags = cpu_save_interrupts();
  preparation.status.phy.state |= WIFI_PHY_BBMCU_READY;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_hw_board(&preparation.board, preparation.cut, deadline, detail)) return false;
  flags = cpu_save_interrupts();
  preparation.status.base.boot.rfe = preparation.board.rfe;
  preparation.status.base.boot.xtal = preparation.board.xtal;
  preparation.status.base.boot.tx_nss = preparation.board.tx_nss;
  preparation.status.base.boot.rx_nss = preparation.board.rx_nss;
  preparation.status.base.boot.tx_antennas = preparation.board.tx_antennas;
  preparation.status.base.boot.rx_antennas = preparation.board.rx_antennas;
  memcpy(preparation.status.base.boot.board_country, preparation.board.country, 2);
  preparation.status.base.boot.valid |= WIFI_BOOT_BOARD_VALID;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_operation_continue()) return false;
  phase(WIFI_PHY_TABLES);
  enum rtl8922ae_tables_result tables = rtl8922ae_firmware_tables(
      preparation.asset.data, preparation.asset.size, preparation.cut,
      preparation.board.rfe, &preparation.tables);
  flags = cpu_save_interrupts();
  switch (tables) {
  case RTL8922AE_TABLES_OK: preparation.status.base.radio.tables_state = WIFI_INPUT_VALID; break;
  case RTL8922AE_TABLES_MISSING: preparation.status.base.radio.tables_state = WIFI_INPUT_ABSENT; break;
  case RTL8922AE_TABLES_MALFORMED: preparation.status.base.radio.tables_state = WIFI_INPUT_MALFORMED; break;
  case RTL8922AE_TABLES_UNSUPPORTED: preparation.status.base.radio.tables_state = WIFI_INPUT_UNSUPPORTED; break;
  }
  if (preparation.tables.default_power_mask & RTL8922AE_TABLES_POWER_2G_MASK) {
    preparation.status.base.radio.tables_flags |= WIFI_TABLE_POWER_DEFAULT;
  }
  cpu_restore_interrupts(flags);
  if (tables != RTL8922AE_TABLES_OK) {
    *detail = "RTL8922AE selected PHY/power tables unavailable";
    return false;
  }
  return true;
}

static bool verify_power(uint64_t deadline, const char **detail)
{
  enum rtl8922ae_power_result result =
      rtl8922ae_power_verify_limits(&preparation.power, deadline, detail);
  if (result != RTL8922AE_POWER_OK) return false;
  uint64_t flags = cpu_save_interrupts();
  struct wifi_phy_facts *phy = &preparation.status.phy;
  phy->state |= WIFI_PHY_POWER_VERIFIED;
  ++phy->power_verifications;
  phy->region = preparation.power.region;
  for (unsigned path = 0; path < 2; ++path) {
    phy->sar_quarter_dbm[path] = preparation.power.sar_quarter_dbm[path];
  }
  cpu_restore_interrupts(flags);
  return true;
}

static bool rfk_checkpoint(bool completed, uint64_t deadline,
    const char **first_failure, const char **detail)
{
  if (!completed && !*first_failure) {
    *first_failure = *detail ? *detail : "RTL8922AE RFK completion failed";
  }
  if (!rtl8922ae_operation_continue()) return false;
  if (!rtl8922ae_transport_poll()) {
    *detail = *first_failure ? *first_failure : "RTL8922AE RFK transport unavailable";
    return false;
  }
  return verify_power(deadline, detail);
}

static bool initialize_phy(uint64_t deadline, const char **detail)
{
  if (!download(deadline, detail)) return false;
  /* Validate every CPU-only RFK and power input before programming the PHY.
   * A table fallback supplies data, never additional regulatory authority. */
  enum rtl8922ae_power_result power = rtl8922ae_power_prepare(
      preparation.asset.data, preparation.asset.size, &preparation.tables,
      &preparation.sar, preparation.platform.rtag.state, preparation.status.base.country,
      &preparation.rules, preparation.channel, &preparation.power);
  if (power != RTL8922AE_POWER_OK) {
    *detail = "RTL8922AE effective power image unsupported or incomplete";
    return false;
  }
  for (unsigned phy = 0; phy < 2; ++phy) {
    if (!rtl8922ae_rfk_prepare(phy, preparation.asset.data, preparation.asset.size,
        &preparation.tables, &preparation.board, preparation.cut, preparation.channel,
        &preparation.rfk[phy], detail)) return false;
  }
  uint64_t flags = cpu_save_interrupts();
  preparation.status.phy.thermal_default_paths = preparation.rfk[0].thermal_default_paths |
      preparation.rfk[1].thermal_default_paths;
  cpu_restore_interrupts(flags);
  phase(WIFI_PHY_MAC);
  if (!rtl8922ae_phy_enable(deadline, detail) || !rtl8922ae_mac_init(deadline, detail)) return false;
  flags = cpu_save_interrupts();
  preparation.status.phy.state |= WIFI_PHY_MAC_CONFIGURED;
  cpu_restore_interrupts(flags);
  phase(WIFI_PHY_HARDWARE);
  if (!rtl8922ae_phy_bb(preparation.asset.data, preparation.asset.size,
          &preparation.tables, deadline, detail) ||
      !rtl8922ae_phy_postinit(deadline, detail) ||
      !rtl8922ae_phy_radio(preparation.asset.data, preparation.asset.size,
          &preparation.tables, deadline, detail) ||
      !rtl8922ae_phy_hardware(preparation.asset.data, preparation.asset.size,
          &preparation.tables, &preparation.board, deadline, detail)) return false;
  flags = cpu_save_interrupts();
  preparation.status.phy.state |= WIFI_PHY_CONFIGURED;
  cpu_restore_interrupts(flags);
  phase(WIFI_PHY_CHANNEL);
  if (!rtl8922ae_mac_channel(preparation.channel, deadline, detail)) return false;
  const uint8_t slots[2] = {0, 0};
  for (unsigned phy = 0; phy < 2; ++phy) {
    if (!rtl8922ae_phy_channel_enter(phy, deadline, detail) ||
        !rtl8922ae_phy_channel_tune(phy, preparation.asset.data, preparation.asset.size,
            &preparation.tables, &preparation.board, preparation.channel, deadline,
            detail)) return false;
    phase(WIFI_PHY_POWER);
    if (rtl8922ae_power_apply(&preparation.power, deadline, detail) != RTL8922AE_POWER_OK ||
        !verify_power(deadline, detail) ||
        !rtl8922ae_phy_channel_leave(phy, slots, deadline, detail)) return false;
  }
  /* Reference late initialization visits PHY0 then PHY1. Preserve warning-only
   * helper handling while keeping request, transport and RF-ceiling gates. */
  const char *rfk_failure = NULL;
  phase(WIFI_PHY_RFK);
  for (unsigned phy = 0; phy < 2; ++phy) {
    bool completed = rtl8922ae_rfk_pre_notify(&preparation.rfk[phy], deadline, detail);
    if (!rfk_checkpoint(completed, deadline, &rfk_failure, detail)) return false;
    completed = rtl8922ae_rfk_dack(phy, deadline, detail);
    if (!rfk_checkpoint(completed, deadline, &rfk_failure, detail)) return false;
    completed = rtl8922ae_rfk_rxdck(&preparation.rfk[phy], deadline, detail);
    if (!rfk_checkpoint(completed, deadline, &rfk_failure, detail)) return false;
  }
  phase(WIFI_PHY_TSSI);
  for (unsigned phy = 0; phy < 2; ++phy) {
    bool completed = rtl8922ae_rfk_tssi(&preparation.rfk[phy], deadline, detail);
    if (!rfk_checkpoint(completed, deadline, &rfk_failure, detail)) return false;
  }
  if (rfk_failure) {
    *detail = rfk_failure;
    return false;
  }
  return rtl8922ae_operation_continue();
}

static void worker_notify(void)
{
  uint64_t flags = cpu_save_interrupts();
  ++preparation.notifications;
  struct task_wait *wait = preparation.worker_wait;
  preparation.worker_wait = NULL;
  if (wait) task_wait_wake(wait);
  cpu_restore_interrupts(flags);
}

void rtl8922ae_runtime_frame(const uint8_t *descriptor, size_t descriptor_bytes,
    const uint8_t *frame, size_t frame_bytes)
{
  rtl8922ae_discovery_receive(&preparation.discovery, descriptor, descriptor_bytes,
      frame, frame_bytes);
}

static void finish_scan(struct wifi_scan_work *work, enum call_status result)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(preparation.pending == work && !preparation.discovery.collecting);
  enum call_status reason = wifi_scan_stop_reason(work);
  if (result != CALL_OUTCOME_UNKNOWN && reason != CALL_OK) result = reason;
  if (result == CALL_OK && preparation.discovery.overflow) result = CALL_FILE_TOO_LARGE;
  preparation.pending = NULL;
  preparation.status.phy.last_status = result;
  preparation.status.phy.phase = WIFI_PHY_COMPLETE;
  wifi_scan_finished(work, result, work->channels,
      result == CALL_OK ? preparation.discovery.records : NULL,
      result == CALL_OK ? preparation.discovery.count : 0);
  cpu_restore_interrupts(flags);
}

static bool terminal_stop(const char *detail)
{
  preparation.stopping = true;
  preparation.rfkill_live = false;
  preparation.live = false;
  rtl8922ae_discovery_stop(&preparation.discovery);
  uint64_t flags = cpu_save_interrupts();
  preparation.status.base.flags &= ~WIFI_READY;
  preparation.status.base.allowed_channels = 0;
  preparation.status.phy.phase = WIFI_PHY_STOPPING;
  cpu_restore_interrupts(flags);
  uint64_t deadline = task_deadline_after_ms(RTL8922AE_STOP_MS);
  const char *stop_detail = "RTL8922AE stop unconfirmed; ownership retained";
  if (preparation.status.phy.state & WIFI_PHY_MAC_CONFIGURED) {
    (void)rtl8922ae_mac_quiesce(deadline, &stop_detail);
  }
  bool stopped = rtl8922ae_transport_stop(deadline, &stop_detail);
  flags = cpu_save_interrupts();
  preparation.status.phy.state |= stopped ? WIFI_PHY_STOP_CONFIRMED : WIFI_PHY_STOP_UNCERTAIN;
  if (preparation.abort_status == CALL_OK || preparation.abort_status == CALL_UNAVAILABLE) {
    failed(detail);
  } else {
    set_detail(detail);
  }
  if (!stopped) {
    preparation.status.base.flags |= WIFI_FAILED;
    if (preparation.abort_status != CALL_OK) set_detail(stop_detail);
  }
  cpu_restore_interrupts(flags);
  return stopped;
}

static bool idle_receive(const char **detail)
{
  uint64_t flags = cpu_save_interrupts();
  uint64_t observed = preparation.notifications;
  cpu_restore_interrupts(flags);
  if (!rtl8922ae_transport_poll()) {
    *detail = "RTL8922AE idle receive transport failed";
    return false;
  }
  if (!rfkill_current(detail)) return false;
  flags = cpu_save_interrupts();
  if (!preparation.pending && observed == preparation.notifications) {
    struct task_wait *wait = task_wait_prepare();
    preparation.worker_wait = wait;
    task_wait_sleep_until(wait, rtl8922ae_deadline(UINT64_MAX, RTL8922AE_RFKILL_NS));
    if (preparation.worker_wait == wait) preparation.worker_wait = NULL;
  }
  cpu_restore_interrupts(flags);
  return true;
}

static void rtl8922ae_worker(void *argument)
{
  (void)argument;
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  const char *detail = "RTL8922AE passive discovery failed";
  for (;;) {
    struct wifi_scan_work *work = preparation.pending;
    if (!work) {
      if (idle_receive(&detail)) continue;
      preparation.abort_status = CALL_UNAVAILABLE;
      bool stopped = terminal_stop(detail);
      if (preparation.pending) {
        finish_scan(preparation.pending, stopped ? CALL_UNAVAILABLE : CALL_OUTCOME_UNKNOWN);
      }
      break;
    }
    uint64_t flags = cpu_save_interrupts();
    bool begin = wifi_scan_begin(work);
    enum call_status reason = wifi_scan_stop_reason(work);
    cpu_restore_interrupts(flags);
    if (!begin || reason != CALL_OK) {
      if (!preparation.live) {
        flags = cpu_save_interrupts();
        preparation.worker_active = false;
        finish_scan(work, reason != CALL_OK ? reason : CALL_DENIED);
        cpu_restore_interrupts(flags);
        return;
      }
      finish_scan(work, reason != CALL_OK ? reason : CALL_DENIED);
      continue;
    }
    preparation.stopping = false;
    bool ready = preparation.live;
    if (!ready) {
      preparation.attempted = true;
      flags = cpu_save_interrupts();
      preparation.status.base.boot.state |= WIFI_BOOT_ATTEMPTED;
      preparation.status.phy.phase = WIFI_PHY_FIRMWARE;
      cpu_restore_interrupts(flags);
      rtl8922ae_rfk_reset();
      rtl8922ae_scan_reset();
      ready = initialize_phy(work->deadline_ns, &detail) &&
          rtl8922ae_mac_scan_prepare(work->deadline_ns, &detail) &&
          rtl8922ae_scan_setup(preparation.board.mac, work->deadline_ns, &detail);
    }
    bool scanning = false, completed = false;
    if (ready && rtl8922ae_transport_retire_receive(work->deadline_ns, &detail)) {
      rtl8922ae_mac_scan_rx_sync(false);
      rtl8922ae_phy_scan_edcca(true);
      scanning = true;
      ready = rtl8922ae_scan_refresh_cam(work->deadline_ns, &detail);
    }
    if (ready && scanning) {
      rtl8922ae_discovery_reset(&preparation.discovery);
      /* This cut stays on the already calibrated channel. Drain preceding RX
       * before opening collection; no descriptor supplies a channel number. */
      rtl8922ae_discovery_channel_start(&preparation.discovery, preparation.channel);
      rtl8922ae_mac_scan_filter(true);
      rtl8922ae_phy_scan_dig();
      completed = rtl8922ae_scan_run(preparation.channel, work->deadline_ns, &detail);
    }
    /* CLOSE/CUT/deadline cannot cancel the independent firmware/RX retirement.
     * A confirmed STOP can preserve a healthy context after cancellation, but
     * it never erases an unconfirmed earlier command or failed END. */
    preparation.stopping = true;
    uint64_t cleanup = task_deadline_after_ms(RTL8922AE_STOP_MS);
    const char *cleanup_detail = "RTL8922AE scan retirement unconfirmed";
    bool retired = ready && rtl8922ae_scan_stop(cleanup, &cleanup_detail);
    if (retired && rtl8922ae_scan_reusable()) {
      if (scanning) {
        rtl8922ae_mac_scan_filter(false);
        retired = rtl8922ae_scan_refresh_cam(cleanup, &cleanup_detail);
        rtl8922ae_phy_scan_edcca(false);
        rtl8922ae_mac_scan_rx_sync(true);
        rtl8922ae_phy_scan_dig();
      }
      retired = retired && rtl8922ae_transport_retire_receive(cleanup, &cleanup_detail);
    } else {
      retired = false;
    }
    rtl8922ae_discovery_stop(&preparation.discovery);
    preparation.stopping = false;
    /* rfkill observed after completion also prevents retaining an RF context. */
    if (retired && !rfkill_current(&cleanup_detail)) {
      preparation.abort_status = CALL_UNAVAILABLE;
      retired = false;
    }
    if (!retired) {
      if (completed && preparation.abort_status == CALL_OK) detail = cleanup_detail;
      bool stopped = terminal_stop(detail);
      enum call_status result = !stopped ? CALL_OUTCOME_UNKNOWN :
          preparation.abort_status != CALL_OK ? preparation.abort_status : CALL_IO;
      finish_scan(work, result);
      break;
    }
    preparation.live = true;
    flags = cpu_save_interrupts();
    preparation.status.base.flags |= WIFI_READY;
    preparation.status.base.allowed_channels = UINT32_C(1) << (preparation.channel - 1);
    set_detail("RTL8922AE one-channel passive discovery; healthy context retained");
    cpu_restore_interrupts(flags);
    enum call_status result = preparation.abort_status != CALL_OK ? preparation.abort_status :
        completed ? CALL_OK : CALL_IO;
    finish_scan(work, result);
  }
  uint64_t flags = cpu_save_interrupts();
  preparation.worker_active = false;
  cpu_restore_interrupts(flags);
}

void rtl8922ae_prepare(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  preparation.status.base.flags = WIFI_ENABLED;
  preparation.status.base.radio.kind = WIFI_RADIO_INPUTS_RTL8922AE;
  preparation.status.base.policy_channels = WIFI_SCAN_CHANNELS;
  memcpy(preparation.status.base.country, "00", 2);
  struct wifi_regdb_profile world;
  KASSERT(wifi_regdb_profile(preparation.status.base.country, &world));
  publish_rules(&world, WIFI_SCAN_CHANNELS);
  if (boot_options_get()->wifi_ax200) {
    failed("Select only one Wi-Fi backend; both radios left untouched");
    return;
  }
  struct pci_device *device = NULL;
  enum pci_selection selection = pci_select_device(RTL8922AE_VENDOR, RTL8922AE_DEVICE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    if (selection != PCI_SELECTION_ABSENT) {
      preparation.status.base.flags |= WIFI_FAILED;
    }
    set_detail(selection == PCI_SELECTION_ABSENT ? "RTL8922AE absent" :
        "RTL8922AE inventory unavailable or ambiguous");
    return;
  }
  preparation.status.base.flags |= WIFI_PRESENT;
  for (size_t i = 0; i < pci_device_count(); ++i) {
    if (pci_device_at(i) == device) {
      preparation.status.base.controller_id = (uint32_t)i + 1;
      break;
    }
  }
  preparation.status.base.pci_bus = device->address.bus;
  preparation.status.base.pci_device = device->address.device;
  preparation.status.base.pci_function = device->address.function;
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
  preparation.status.base.flags |= WIFI_PREPARED;
  set_detail("RTL8922AE prepared; explicit SCAN required for DBCC initialization");
}

void rtl8922ae_start(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled() && !preparation.started);
  preparation.started = true;

}

void rtl8922ae_interrupt(void)
{
  rtl8922ae_transport_interrupt();
  worker_notify();
}

void rtl8922ae_status(struct wifi_status_full_reply *status)
{
  *status = preparation.status;
  rtl8922ae_transport_status(&status->base);
  if (preparation.attempted) rtl8922ae_scan_status(&status->base);
  struct rtl8922ae_rfk_observations rfk;
  rtl8922ae_rfk_observe(&rfk);
  status->phy.rfk_completed = rfk.completed;
  if (rfk.state_valid) {
    status->phy.state |= WIFI_PHY_RFK_REPORT_VALID;
    status->phy.rfk_state = rfk.state;
    status->phy.rfk_version = rfk.version;
  }
  enum acpi_wifi_state state = acpi_rtl_wifi_snapshot(&preparation.platform);
  struct wifi_radio_inputs *radio = &status->base.radio;
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
  if (!(preparation.status.base.flags & WIFI_PRESENT)) {
    return CALL_UNAVAILABLE;
  }
  if (preparation.pending || preparation.attempted) return CALL_BUSY;
  if (preparation.status.base.flags & WIFI_POLICY_INSTALLED) {
    return CALL_ALREADY_EXISTS;
  }
  memcpy(preparation.status.base.country, policy->country, 2);
  preparation.status.base.policy_channels = policy->channels;
  preparation.status.base.flags |= WIFI_POLICY_INSTALLED;
  publish_rules(&profile, policy->channels);
  return CALL_OK;
}


enum call_status rtl8922ae_scan(struct wifi_scan_work *work)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  if (preparation.pending) return CALL_BUSY;
  if (!preparation.started || !preparation.prepared ||
      (preparation.attempted && !preparation.live) ||
      (preparation.status.base.flags & WIFI_FAILED)) return CALL_UNAVAILABLE;
  enum acpi_wifi_state captured = acpi_rtl_wifi_snapshot(&preparation.platform);
  preparation.sar = rtl8922ae_sar_envelope(&preparation.platform, captured);
  if (captured != ACPI_WIFI_VALID || preparation.sar.state != ACPI_WIFI_VALID) {
    set_detail("RTL8922AE required platform/SAR inputs unavailable; no firmware/RF startup");
    return CALL_UNAVAILABLE;
  }
  if (!wifi_regdb_profile(preparation.status.base.country, &preparation.rules)) return CALL_BAD_REQUEST;
  uint32_t admitted = preparation.rules.channels & preparation.status.base.policy_channels;
  if (!admitted) return CALL_UNAVAILABLE;
  if (work->channels & ~admitted) return CALL_BAD_REQUEST;
  /* One explicit channel is the qualified delivery boundary. Do not silently
   * replace a caller's complete/default mask with a one-channel result. */
  if (!work->channels || (work->channels & (work->channels - 1))) {
    set_detail("RTL8922AE first scan slice requires one explicit channel");
    return CALL_UNAVAILABLE;
  }
  uint8_t channel = 0;
  for (unsigned i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    /* No environment/PtP qualification exists in this cut. AUTO_BW/NO_HT40
     * do not restrict this fixed 20 MHz profile; every other restriction does. */
    uint32_t restrictive = preparation.rules.channel[i].flags &
        ~(WIFI_RULE_AUTO_BW | WIFI_RULE_NO_HT40);
    if ((work->channels & (UINT32_C(1) << i)) && !restrictive) {
      channel = i + 1;
      break;
    }
  }
  if (!channel) {
    set_detail("RTL8922AE request has no qualified calibration channel");
    return CALL_UNAVAILABLE;
  }
  if (preparation.live && channel != preparation.channel) {
    set_detail("RTL8922AE retained scan context supports its calibrated channel only");
    return CALL_UNAVAILABLE;
  }
  preparation.channel = channel;
  uint64_t maximum = task_deadline_after_ms(RTL8922AE_SCAN_MS);
  if (!work->deadline_ns || work->deadline_ns > maximum) work->deadline_ns = maximum;
  if (task_deadline_expired(work->deadline_ns)) return CALL_TIMED_OUT;
  preparation.abort_status = CALL_OK;
  if (!preparation.live) {
    preparation.status.phy = (struct wifi_phy_facts){.channel = preparation.channel};
  }
  preparation.status.phy.last_status = CALL_OK;
  preparation.pending = work;
  if (!preparation.worker_active) {
    if (kernel_task_create(rtl8922ae_worker, NULL) != MM_OK) {
      preparation.pending = NULL;
      return CALL_NO_MEMORY;
    }
    preparation.worker_active = true;
  }
  worker_notify();
  return CALL_OK;
}

void rtl8922ae_cancel(struct wifi_scan_work *work)
{
  atomic_store_explicit(&work->cancel, true, memory_order_release);
}
