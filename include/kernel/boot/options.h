#ifndef KERNEL_BOOT_OPTIONS_H
#define KERNEL_BOOT_OPTIONS_H

#include <kernel/object/mount.h>

struct boot_options {
  const char *init;
  struct mount_config mount;
  bool install;
  bool default_config;
  const char *remote_beacon;
  bool log_udp;
  const char *debug_net;
  bool debug_wait;
  uint8_t debug_image[32];
  const char *display_size;
  const char *display_timing;
  bool display_timing_metrics;
  bool display_inventory;
  bool display_flip, display_flip_metrics;
  bool display_cursor_probe;
  bool pointer_synthetic;
};

/* BSP, IF=0: call once before display/AP initialization. Copies the command
 * line into bounded static storage; returned options and strings live for
 * this boot. Invalid or duplicate options are fatal. The display driver owns
 * display.size value validation and its nonfatal fallback. */
const struct boot_options *boot_options_parse(const char *command_line);
const struct boot_options *boot_options_get(void);

#endif
