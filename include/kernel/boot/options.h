#ifndef KERNEL_BOOT_OPTIONS_H
#define KERNEL_BOOT_OPTIONS_H

#include <abi/log.h>
#include <kernel/object/mount.h>

/* Idle period for display.idle_blank when absent or invalid, and after a wake
 * from the initial blank of display.idle_blank=0. */
#define DISPLAY_IDLE_BLANK_DEFAULT_SECONDS 300

struct boot_options {
  const char *init;
  struct mount_config mount;
  /* mount.pool: the native pool's one-based GPT partition on mount.disk, for
   * boot init to find its configuration; zero when absent or malformed.
   * POOL_MALFORMED marks a present but malformed value with mount.disk bound. */
  uint32_t pool_partition;
  bool pool_malformed;
  bool install;
  bool nvme_write;
  bool wifi_ax200;
  bool wifi_rtl8922ae;
  bool usb_hid_describe;
  bool usb_xhci_diagnose;
  bool ps2_synaptics, ps2_synaptics_diagnose;
  bool default_config;
  const char *remote_beacon;
  bool log_udp;
  enum log_level log_level;
  const char *debug_net;
  bool debug_wait;
  uint8_t debug_image[32];
  const char *display_size;
  const char *display_timing;
  bool display_timing_metrics;
  /* Disabled by display.idle_blank=off; zero seconds blanks at presentation start. */
  bool display_idle_blank;
  uint32_t display_idle_blank_seconds;
  bool display_inventory;
  bool display_flip, display_flip_metrics;
  bool display_cursor_probe;
  bool pointer_synthetic;
};

/* BSP, IF=0: call once before display/AP initialization. Copies the command
 * line into bounded static storage; returned options and strings live for
 * this boot. Unknown/invalid options warn and fall back; duplicates use their
 * last occurrence. Overflow is fatal: truncation could change authority.
 * Display backends retain geometry/capacity validation and nonfatal refusal. */
const struct boot_options *boot_options_parse(const char *command_line);
const struct boot_options *boot_options_get(void);

#endif
