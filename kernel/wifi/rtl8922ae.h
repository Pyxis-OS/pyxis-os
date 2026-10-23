#ifndef KERNEL_WIFI_RTL8922AE_H
#define KERNEL_WIFI_RTL8922AE_H

#include <abi/wifi.h>
#include <abi/syscall.h>

struct boot_info;
struct wifi_scan_work;
/* Opt-in boot reservation, followed by one request-driven preparation attempt.
 * No startup on READ, scan FILE or packet interface is provided in this cut. */
void rtl8922ae_prepare(const struct boot_info *boot);
void rtl8922ae_start(void);
void rtl8922ae_interrupt(void);
void rtl8922ae_status(struct wifi_status_reply *status);
enum call_status rtl8922ae_policy(const struct wifi_policy_request *policy);
enum call_status rtl8922ae_scan(struct wifi_scan_work *work);
void rtl8922ae_cancel(struct wifi_scan_work *work);

#endif
