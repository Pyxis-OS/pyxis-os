#ifndef KERNEL_WIFI_RTL8922AE_H
#define KERNEL_WIFI_RTL8922AE_H

#include <abi/wifi.h>
#include <abi/syscall.h>

#define RTL8922AE_CHANNELS_2G 13u
#define RTL8922AE_SCAN_CHANNELS ((UINT32_C(1) << RTL8922AE_CHANNELS_2G) - 1)

struct boot_info;
struct wifi_scan_work;
/* Opt-in boot reservation, followed by request-driven preparation and bounded
 * passive discovery. READ never starts hardware; no station/data interface. */
void rtl8922ae_prepare(const struct boot_info *boot);
void rtl8922ae_start(void);
void rtl8922ae_interrupt(void);
void rtl8922ae_status(struct wifi_status_full_reply *status);
enum call_status rtl8922ae_policy(const struct wifi_policy_request *policy);
enum call_status rtl8922ae_scan(struct wifi_scan_work *work);
void rtl8922ae_cancel(struct wifi_scan_work *work);

#endif
