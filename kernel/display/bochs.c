#include "bochs.h"
#include "internal.h"
#include <arch/bochs_display.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/space.h>

#define DISPLAY_PIXEL_BYTES 4u

static struct framebuffer target;

bool bochs_display_matches(const struct pci_device *device)
{
  return arch_bochs_display_matches(device);
}

static bool parse_dimension(const char **text, uint32_t *value)
{
  const char *cursor = *text;
  uint32_t parsed = 0;
  if (*cursor < '0' || *cursor > '9') {
    return false;
  }
  do {
    unsigned digit = *cursor - '0';
    if (parsed > (UINT32_MAX - digit) / 10) {
      return false;
    }
    parsed = parsed * 10 + digit;
    ++cursor;
  } while (*cursor >= '0' && *cursor <= '9');
  *text = cursor;
  *value = parsed;
  return parsed != 0;
}

static const struct framebuffer *refuse(const char *reason)
{
  klog("Bochs display: %s; keeping firmware framebuffer\n", reason);
  return NULL;
}

const struct framebuffer *bochs_display_prepare(const struct boot_info *boot,
    struct pci_device *device, const char *size)
{
  if (!device) {
    return NULL;
  }
  if (!size) {
    ktrace("Bochs display: no mode requested; using firmware framebuffer\n");
    return NULL;
  }
  uint32_t width, height;
  const char *cursor = size;
  if (!parse_dimension(&cursor, &width) || *cursor++ != 'x' ||
      !parse_dimension(&cursor, &height) || *cursor) {
    return refuse("display.size must be positive WIDTHxHEIGHT");
  }
  if (!space_display_size_supported(width, height)) {
    return refuse("display.size is too small for screen navigation");
  }
  if (!display_modeset_begin()) {
    return NULL;
  }

  uintptr_t address;
  const char *reason;
  enum bochs_display_result result = arch_bochs_display_prepare(boot, device,
      width, height, &address, &reason);
  if (result == BOCHS_DISPLAY_RESTORE_FAILED) {
    panic("Bochs display: cannot verify firmware scanout restoration (%s)", reason);
  }
  if (result != BOCHS_DISPLAY_READY) {
    return refuse(reason);
  }
  target = (struct framebuffer){
    .address = address,
    .size = (size_t)width * DISPLAY_PIXEL_BYTES * height,
    .pitch = (size_t)width * DISPLAY_PIXEL_BYTES,
    .width = width,
    .height = height,
    .red_shift = 16,
    .green_shift = 8,
    .blue_shift = 0,
  };
  ktrace("Bochs display: selected %ux%u, pitch=%zu\n", width, height, target.pitch);
  return &target;
}
