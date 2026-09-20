//
// Created by chronium on 9/18/26.
//

#include <kernel/fb/fb.h>

void fb_rect(
    struct framebuffer *fb,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    uint32_t color)
{
    if (!fb || width == 0 || height == 0)
        return;

    if (x >= fb->width || y >= fb->height)
        return;

    if (width > fb->width - x)
        width = fb->width - x;

    if (height > fb->height - y)
        height = fb->height - y;

    uint8_t *base = (uint8_t *)fb->address;

    uint32_t *top =
        (uint32_t *)(base + y * fb->pitch + x * sizeof(uint32_t));

    uint32_t *bottom =
        (uint32_t *)(base +
                     (y + height - 1) * fb->pitch +
                     x * sizeof(uint32_t));

    for (size_t i = 0; i < width; ++i) {
        top[i] = color;

        if (height > 1)
            bottom[i] = color;
    }

    if (height <= 2)
        return;

    for (size_t row = 1; row < height - 1; ++row) {
        uint32_t *pixels =
            (uint32_t *)(base +
                         (y + row) * fb->pitch +
                         x * sizeof(uint32_t));

        pixels[0] = color;

        if (width > 1)
            pixels[width - 1] = color;
    }
}

void fb_fill_rect(
    struct framebuffer *fb,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    uint32_t color)
{
    if (!fb || width == 0 || height == 0)
        return;

    if (x >= fb->width || y >= fb->height)
        return;

    if (width > fb->width - x)
        width = fb->width - x;

    if (height > fb->height - y)
        height = fb->height - y;

    uint8_t *base = (uint8_t *)fb->address;

    for (size_t row = 0; row < height; ++row) {
        uint32_t *pixels =
            (uint32_t *)(base +
                         (y + row) * fb->pitch +
                         x * sizeof(uint32_t));

        for (size_t col = 0; col < width; ++col)
            pixels[col] = color;
    }
}
