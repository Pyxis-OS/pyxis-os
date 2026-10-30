#include <arch/smp.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/boot.h>
#include <kernel/boot/options.h>
#include <kernel/panic.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/user/launch.h>
#include <remote/beacon.h>
#include <pointer-synthetic-config.h>

static char command_line_storage[BOOT_COMMAND_LINE_SIZE];
static struct boot_options options;
static bool parsed;

static bool same_text(const char *left, const char *right)
{
  size_t size = strlen(left);
  return size == strlen(right) && !memcmp(left, right, size);
}

static int hex_digit(char value)
{
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

static bool parse_disk_guid(const char *text, struct gpt_guid *disk)
{
  if (strlen(text) != 36 || text[8] != '-' || text[13] != '-' ||
      text[18] != '-' || text[23] != '-') {
    return false;
  }
  uint8_t canonical[16];
  unsigned index = 0;
  for (size_t offset = 0; offset < 36;) {
    if (offset == 8 || offset == 13 || offset == 18 || offset == 23) {
      ++offset;
      continue;
    }
    int high = hex_digit(text[offset]), low = hex_digit(text[offset + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    canonical[index++] = high * 16 + low;
    offset += 2;
  }
  /* The first three textual fields use little-endian GPT encoding. */
  for (unsigned i = 0; i < 4; ++i) {
    disk->bytes[i] = canonical[3 - i];
  }
  for (unsigned i = 0; i < 2; ++i) {
    disk->bytes[4 + i] = canonical[5 - i];
    disk->bytes[6 + i] = canonical[7 - i];
  }
  memcpy(disk->bytes + 8, canonical + 8, 8);
  struct gpt_guid zero = {0};
  return memcmp(disk, &zero, sizeof(*disk)) != 0;
}

static bool parse_debug_image(const char *text, uint8_t image[32])
{
  if (strlen(text) != 64) {
    return false;
  }
  for (unsigned i = 0; i < 32; ++i) {
    int high = hex_digit(text[i * 2]), low = hex_digit(text[i * 2 + 1]);
    if (high < 0 || low < 0) {
      return false;
    }
    image[i] = high * 16 + low;
  }
  return true;
}

static void take_option(const char **option, const char *key, const char *value)
{
  if (*option) {
    kwarn("boot options: duplicate %s; using last occurrence\n", key);
  }
  *option = value;
}

static bool boolean_value(const char *value)
{
  return same_text(value, "0") || same_text(value, "1");
}

static bool flag_option(const char *key, const char *value, bool fallback)
{
  if (!value) {
    return fallback;
  }
  if (!boolean_value(value)) {
    kwarn("boot options: %s expects 0 or 1; using default %u\n", key, (unsigned)fallback);
    return fallback;
  }
  return same_text(value, "1");
}

static const char *const log_level_names[] = {
  [LOG_LEVEL_ERROR] = "error",
  [LOG_LEVEL_WARN] = "warn",
  [LOG_LEVEL_INFO] = "info",
  [LOG_LEVEL_DEBUG] = "debug",
  [LOG_LEVEL_TRACE] = "trace",
};

static enum log_level log_level_option(const char *value)
{
  if (!value) {
    return LOG_LEVEL_INFO;
  }
  for (enum log_level level = LOG_LEVEL_ERROR; level <= LOG_LEVEL_TRACE; ++level) {
    if (!same_text(value, log_level_names[level])) {
      continue;
    }
    if (level > LOG_LEVEL_CEILING) {
      kwarn("boot options: log.level=%s is deeper than this build's %s ceiling; using %s\n",
          value, log_level_names[LOG_LEVEL_CEILING], log_level_names[LOG_LEVEL_CEILING]);
      return LOG_LEVEL_CEILING;
    }
    return level;
  }
  kwarn("boot options: log.level expects error, warn, info, debug or trace; using info\n");
  return LOG_LEVEL_INFO;
}

static bool display_size_valid(const char *text)
{
  for (unsigned dimension = 0; dimension < 2; ++dimension) {
    uint32_t value = 0;
    if (*text < '0' || *text > '9') {
      return false;
    }
    do {
      unsigned digit = *text - '0';
      if (value > (UINT32_MAX - digit) / 10) {
        return false;
      }
      value = value * 10 + digit;
      ++text;
    } while (*text >= '0' && *text <= '9');
    if (!value || (dimension == 0 ? *text != 'x' : *text != '\0')) {
      return false;
    }
    if (dimension == 0) {
      ++text;
    }
  }
  return true;
}

/* A decimal number that fits a uint32; nothing else is accepted. */
static bool parse_decimal(const char *text, uint32_t *output)
{
  if (*text < '0' || *text > '9') {
    return false;
  }
  uint32_t value = 0;
  do {
    unsigned digit = *text - '0';
    if (value > (UINT32_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
    ++text;
  } while (*text >= '0' && *text <= '9');
  *output = value;
  return *text == '\0';
}

static void configure_idle_blank(const char *value)
{
  options.display_idle_blank = true;
  options.display_idle_blank_seconds = DISPLAY_IDLE_BLANK_DEFAULT_SECONDS;
  if (!value) {
    return;
  }
  if (same_text(value, "off")) {
    options.display_idle_blank = false;
  } else if (!parse_decimal(value, &options.display_idle_blank_seconds)) {
    kwarn("boot options: display.idle_blank expects SECONDS or off; using %u seconds\n",
        DISPLAY_IDLE_BLANK_DEFAULT_SECONDS);
    options.display_idle_blank_seconds = DISPLAY_IDLE_BLANK_DEFAULT_SECONDS;
  }
}

static void configure_debugger(const char *net, const char *image, const char *wait)
{
  if (!net && !image && !wait) {
    return;
  }
  bool valid = true;
  if (!remote_beacon_name_length(net)) {
    kwarn("boot options: debug.net needs 1..%u printable ASCII bytes without spaces\n",
        REMOTE_BEACON_NAME_MAX);
    valid = false;
  }
  uint8_t parsed_image[32];
  if (!image || !parse_debug_image(image, parsed_image)) {
    kwarn("boot options: debug.image needs 64 hexadecimal SHA-256 digits\n");
    valid = false;
  }
  bool parsed_wait = flag_option("debug.wait", wait, false);
  if (wait && !boolean_value(wait)) {
    valid = false;
  }
  if (!valid) {
    kwarn("boot options: invalid or incomplete debug settings; debugger and wait off\n");
    return;
  }
  options.debug_net = net;
  options.debug_wait = parsed_wait;
  memcpy(options.debug_image, parsed_image, sizeof(parsed_image));
}

const struct boot_options *boot_options_parse(const char *command_line)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!arch_cpu_count() && !parsed);
  parsed = true;

  size_t length = 0;
  while (length < sizeof(command_line_storage) && command_line[length]) {
    command_line_storage[length] = command_line[length];
    ++length;
  }
  if (length == sizeof(command_line_storage)) {
    panic("kernel command line exceeds %zu bytes", sizeof(command_line_storage) - 1);
  }
  command_line_storage[length] = '\0';

  const char *init = NULL, *mount_disk = NULL, *mount_pool = NULL, *install = NULL;
  const char *default_config = NULL;
  const char *remote_beacon = NULL, *log_udp = NULL, *log_level = NULL, *display_size = NULL;
  const char *display_timing = NULL, *display_timing_metrics = NULL, *display_inventory = NULL;
  const char *display_idle_blank = NULL;
  const char *debug_net = NULL, *debug_wait = NULL, *debug_image = NULL;
  const char *display_flip = NULL, *display_flip_metrics = NULL;
  const char *display_cursor_probe = NULL;
  const char *usb_hid_describe = NULL;
  const char *usb_xhci_diagnose = NULL;
  const char *nvme_write = NULL, *wifi_ax200 = NULL, *wifi_rtl8922ae = NULL;
  const char *ps2_synaptics = NULL, *ps2_synaptics_diagnose = NULL;
#if POINTER_SYNTHETIC_ENABLED
  const char *pointer_synthetic = NULL;
#endif

  char *cursor = command_line_storage;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    if (!*cursor) {
      break;
    }
    char *key = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') {
      ++cursor;
    }
    if (*cursor) {
      *cursor++ = '\0';
    }
    char *value = key;
    while (*value && *value != '=') {
      ++value;
    }
    if (*value) {
      *value++ = '\0';
    }
    if (same_text(key, "init")) {
      take_option(&init, key, value);
    } else if (same_text(key, "mount.disk")) {
      take_option(&mount_disk, key, value);
    } else if (same_text(key, "mount.pool")) {
      take_option(&mount_pool, key, value);
    } else if (same_text(key, "boot.install")) {
      take_option(&install, key, value);
    } else if (same_text(key, "usb.hid.describe")) {
      take_option(&usb_hid_describe, key, value);
    } else if (same_text(key, "usb.xhci.diagnose")) {
      take_option(&usb_xhci_diagnose, key, value);
    } else if (same_text(key, "ps2.synaptics")) {
      take_option(&ps2_synaptics, key, value);
    } else if (same_text(key, "ps2.synaptics.diagnose")) {
      take_option(&ps2_synaptics_diagnose, key, value);
    } else if (same_text(key, "wifi.ax200")) {
      take_option(&wifi_ax200, key, value);
    } else if (same_text(key, "wifi.rtl8922ae")) {
      take_option(&wifi_rtl8922ae, key, value);
    } else if (same_text(key, "nvme.write")) {
      take_option(&nvme_write, key, value);
    } else if (same_text(key, "boot.default_config")) {
      take_option(&default_config, key, value);
    } else if (same_text(key, "remote.beacon")) {
      take_option(&remote_beacon, key, value);
    } else if (same_text(key, "log.udp")) {
      take_option(&log_udp, key, value);
    } else if (same_text(key, "log.level")) {
      take_option(&log_level, key, value);
    } else if (same_text(key, "debug.net")) {
      take_option(&debug_net, key, value);
    } else if (same_text(key, "debug.wait")) {
      take_option(&debug_wait, key, value);
    } else if (same_text(key, "debug.image")) {
      take_option(&debug_image, key, value);
    } else if (same_text(key, "display.size")) {
      take_option(&display_size, key, value);
    } else if (same_text(key, "display.timing")) {
      take_option(&display_timing, key, value);
    } else if (same_text(key, "display.timing.metrics")) {
      take_option(&display_timing_metrics, key, value);
    } else if (same_text(key, "display.idle_blank")) {
      take_option(&display_idle_blank, key, value);
    } else if (same_text(key, "display.flip")) {
      take_option(&display_flip, key, value);
    } else if (same_text(key, "display.flip.metrics")) {
      take_option(&display_flip_metrics, key, value);
    } else if (same_text(key, "display.inventory")) {
      take_option(&display_inventory, key, value);
    } else if (same_text(key, "display.cursor.probe")) {
      take_option(&display_cursor_probe, key, value);
#if POINTER_SYNTHETIC_ENABLED
    } else if (same_text(key, "pointer.synthetic")) {
      take_option(&pointer_synthetic, key, value);
#endif
    } else {
      kwarn("boot options: unknown option %s; ignored\n", key);
    }
  }
  if (init && (strlen(init) <= USER_BOOT_ROOT_PREFIX_LENGTH ||
      memcmp(init, USER_BOOT_ROOT_PREFIX, USER_BOOT_ROOT_PREFIX_LENGTH))) {
    kwarn("boot options: init must name a " USER_BOOT_ROOT_PREFIX
        " archive entry; using " USER_DEFAULT_BOOT_INIT "\n");
    init = NULL;
  }
  options.init = init ? init : USER_DEFAULT_BOOT_INIT;

  if (mount_disk) {
    struct gpt_guid disk;
    if (parse_disk_guid(mount_disk, &disk)) {
      options.mount.disk = disk;
      options.mount.enabled = true;
    } else {
      kwarn("boot options: mount.disk needs a canonical nonzero GPT GUID; mounting nothing\n");
    }
  }
  if (mount_pool && !options.mount.enabled) {
    kwarn("boot options: mount.pool needs mount.disk; ignored\n");
  } else if (mount_pool &&
      (!parse_decimal(mount_pool, &options.pool_partition) || !options.pool_partition)) {
    kwarn("boot options: mount.pool needs a one-based GPT partition number; boot init starts rescue\n");
    options.pool_partition = 0;
    options.pool_malformed = true;
  }
  options.install = flag_option("boot.install", install, false);
  options.nvme_write = flag_option("nvme.write", nvme_write, true);
  options.wifi_ax200 = flag_option("wifi.ax200", wifi_ax200, false);
  options.wifi_rtl8922ae = flag_option("wifi.rtl8922ae", wifi_rtl8922ae, false);
  options.default_config = flag_option("boot.default_config", default_config, false);
  options.log_udp = flag_option("log.udp", log_udp, false);
  options.log_level = log_level_option(log_level);
  if (remote_beacon && !remote_beacon_name_length(remote_beacon)) {
    kwarn("boot options: remote.beacon needs 1..%u printable ASCII bytes without spaces; ignored\n",
        REMOTE_BEACON_NAME_MAX);
    remote_beacon = NULL;
  }
  options.remote_beacon = remote_beacon;
  configure_debugger(debug_net, debug_image, debug_wait);

  if (display_size && !display_size_valid(display_size)) {
    kwarn("boot options: display.size needs positive uint32 WIDTHxHEIGHT; keeping firmware geometry\n");
    display_size = NULL;
  }
  options.display_size = display_size;
  if (display_timing && !same_text(display_timing, "off") &&
      !same_text(display_timing, "observe") && !same_text(display_timing, "blank")) {
    kwarn("boot options: display.timing expects off, observe or blank; using observe\n");
    display_timing = NULL;
  }
  options.display_timing = display_timing;
  options.display_timing_metrics = flag_option("display.timing.metrics", display_timing_metrics, false);
  configure_idle_blank(display_idle_blank);
  options.display_inventory = flag_option("display.inventory", display_inventory, false);
  options.display_flip = flag_option("display.flip", display_flip, true);
  options.display_flip_metrics = flag_option("display.flip.metrics", display_flip_metrics, false);
  options.ps2_synaptics = flag_option("ps2.synaptics", ps2_synaptics, true);
  options.ps2_synaptics_diagnose = flag_option("ps2.synaptics.diagnose", ps2_synaptics_diagnose, false);
  options.usb_hid_describe = flag_option("usb.hid.describe", usb_hid_describe, false);
  options.usb_xhci_diagnose = flag_option("usb.xhci.diagnose", usb_xhci_diagnose, false);
  options.display_cursor_probe = flag_option("display.cursor.probe", display_cursor_probe, false);
#if POINTER_SYNTHETIC_ENABLED
  if (pointer_synthetic) {
    if (same_text(pointer_synthetic, "schedule")) {
      options.pointer_synthetic = true;
    } else {
      kwarn("boot options: pointer.synthetic expects schedule; disabled\n");
    }
  }
#endif
  return &options;
}

const struct boot_options *boot_options_get(void)
{
  KASSERT(parsed);
  return &options;
}
