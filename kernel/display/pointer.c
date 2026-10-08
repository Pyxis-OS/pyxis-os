#include <kernel/display_capture.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/pointer_present.h>
#include <kernel/string.h>

#define POINTER_CHANNEL_MAX 255

static uint32_t blend_channel(uint8_t foreground, uint32_t background, uint8_t alpha)
{
  return (foreground * alpha + background * (POINTER_CHANNEL_MAX - alpha) +
      POINTER_CHANNEL_MAX / 2) / POINTER_CHANNEL_MAX;
}

static uint32_t blend_pixel(const struct framebuffer *layout,
    uint32_t background, const uint8_t *foreground)
{
  uint8_t alpha = foreground[3];
  if (!alpha) {
    return background;
  }

  uint32_t red = blend_channel(foreground[2],
      (background >> layout->red_shift) & POINTER_CHANNEL_MAX, alpha);
  uint32_t green = blend_channel(foreground[1],
      (background >> layout->green_shift) & POINTER_CHANNEL_MAX, alpha);
  uint32_t blue = blend_channel(foreground[0],
      (background >> layout->blue_shift) & POINTER_CHANNEL_MAX, alpha);
  uint32_t mask = (UINT32_C(255) << layout->red_shift) |
      (UINT32_C(255) << layout->green_shift) | (UINT32_C(255) << layout->blue_shift);
  return (background & ~mask) | (red << layout->red_shift) |
      (green << layout->green_shift) | (blue << layout->blue_shift);
}

void pointer_present_copy(const struct framebuffer *layout,
    const struct pointer_frame *frame, size_t offset, const void *pixels, size_t bytes)
{
  KASSERT(offset <= layout->size && bytes <= layout->size - offset);
  KASSERT(!(offset % sizeof(uint32_t)) && !(bytes % sizeof(uint32_t)));
  if (!frame->visible || !bytes) {
    screen_capture_copy(offset, pixels, bytes);
    return;
  }
  KASSERT(frame->pixels && frame->width && frame->width <= POINTER_IMAGE_MAX &&
      frame->height && frame->height <= POINTER_IMAGE_MAX);
  KASSERT(frame->hotspot_x < frame->width && frame->hotspot_y < frame->height);
  KASSERT(frame->x >= 0 && frame->y >= 0);

  /* Position is a clamped physical hotspot. Subtract before clipping so the
   * image can extend above or left of the screen without unsigned wrapping. */
  int64_t left = frame->x - frame->hotspot_x;
  int64_t top = frame->y - frame->hotspot_y;
  int64_t right = left + frame->width;
  int64_t bottom = top + frame->height;
  if (right <= 0 || bottom <= 0 || left >= (int64_t)layout->width ||
      top >= (int64_t)layout->height) {
    screen_capture_copy(offset, pixels, bytes);
    return;
  }

  size_t first_x = left > 0 ? (size_t)left : 0;
  size_t first_y = top > 0 ? (size_t)top : 0;
  size_t last_x = MIN((size_t)right, layout->width);
  size_t last_y = MIN((size_t)bottom, layout->height);
  size_t end = offset + bytes;
  const uint8_t *source = pixels;
  uint32_t staged[POINTER_IMAGE_MAX];

  for (size_t y = first_y; y < last_y; ++y) {
    size_t row_start = y * layout->pitch + first_x * sizeof(uint32_t);
    size_t row_end = y * layout->pitch + last_x * sizeof(uint32_t);
    if (row_end <= offset) {
      continue;
    }
    if (row_start >= end) {
      break;
    }
    if (offset < row_start) {
      size_t prefix = row_start - offset;
      screen_capture_copy(offset, source, prefix);
      source += prefix;
      offset += prefix;
    }

    size_t count = MIN(row_end, end) - offset;
    size_t columns = count / sizeof(uint32_t);
    size_t screen_x = first_x + (offset - row_start) / sizeof(uint32_t);
    size_t image_x = (size_t)((int64_t)screen_x - left);
    size_t image_y = (size_t)((int64_t)y - top);
    const uint8_t *image = frame->pixels +
        (image_y * frame->width + image_x) * sizeof(uint32_t);
    memcpy(staged, source, count);
    for (size_t x = 0; x < columns; ++x) {
      staged[x] = blend_pixel(layout, staged[x], image + x * sizeof(uint32_t));
    }
    screen_capture_copy(offset, staged, count);
    source += count;
    offset += count;
  }
  /* This also forwards device row padding without blending or compacting it. */
  screen_capture_copy(offset, source, end - offset);
}
