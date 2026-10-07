#include "boot.h"
#include <kernel/memory.h>

static struct framebuffer target;

const struct framebuffer *boot_display_init(const struct boot_framebuffer *boot)
{
  target = (struct framebuffer){
    .address = boot->address,
    .size = boot->size,
    .pitch = boot->pitch,
    .width = boot->width,
    .height = boot->height,
    .red_shift = boot->red_shift,
    .green_shift = boot->green_shift,
    .blue_shift = boot->blue_shift,
  };
  return &target;
}

void boot_display_copy(size_t offset, const void *pixels, size_t bytes)
{
  memcpy((void *)(target.address + offset), pixels, bytes);
}
