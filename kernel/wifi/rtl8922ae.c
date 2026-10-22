#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/boot/options.h>
#include <kernel/initrd.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "rtl8922ae.h"
#include "rtl8922ae_firmware.h"
#include "rtl8922ae_hw.h"
#include "rtl8922ae_transport.h"

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
  bool prepared, started;
  uint8_t cut;
} preparation;

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
    set_detail("RTL8922AE firmware ready; MAC off and PCI reset confirmed; scan/data unavailable");
  }
  cpu_restore_interrupts(flags);
}

void rtl8922ae_prepare(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  preparation.status.flags = WIFI_ENABLED;
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
}
