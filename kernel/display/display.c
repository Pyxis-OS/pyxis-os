#include <arch/cpu.h>
#include <kernel/display.h>
#include <kernel/fb/early_console.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <stdatomic.h>
#include "boot.h"
#include "virtio_gpu.h"
#include "internal.h"

#define DISPLAY_COPY_BYTES (64 * 1024)
#define DISPLAY_PANIC_WAIT_LIMIT 1000000
#define DISPLAY_NO_WRITER UINT32_MAX

enum display_driver { DISPLAY_BOOT, DISPLAY_VIRTIO_GPU };
static enum display_driver driver;
static const struct framebuffer *target;
static bool available, failed;
static atomic_bool panic_claimed;
/* Sequential consistency pairs writer publication/recheck with panic's
 * claim/load. Either the writer sees the claim or the claimant sees it. */
static _Atomic uint32_t writer = DISPLAY_NO_WRITER;

void display_init(const struct boot_info *boot)
{
  if (boot->framebuffer.size) {
    target = boot_display_init(&boot->framebuffer);
  }
  bool selected;
  const struct framebuffer *gpu = virtio_gpu_prepare(boot, &selected);
  if (selected) {
    driver = DISPLAY_VIRTIO_GPU;
    failed = !gpu;
    if (gpu) {
      target = gpu;
    }
    /* Firmware scanout cannot survive the transport reset. Retire drawing before
     * AP startup; VirtIO panic reporting remains serial-only. */
    bool locked = log_begin();
    if (locked) {
      early_console_retire();
    }
    log_end(locked);
  }
  if (!target) {
    panic("no usable display: firmware framebuffer or supported driver required");
  }
}

bool display_start(void)
{
  switch (driver) {
    case DISPLAY_BOOT:
      available = true;
      break;
    case DISPLAY_VIRTIO_GPU:
      available = virtio_gpu_start();
      break;
  }
  failed = !available;
  return available;
}

bool display_available(void)
{
  return !failed && !display_is_panicking();
}

void display_interrupt(void)
{
  virtio_gpu_interrupt();
}

bool display_is_panicking(void)
{
  return atomic_load(&panic_claimed);
}

const struct framebuffer *display_layout(void)
{
  return target;
}

bool display_begin_frame(void)
{
  atomic_store(&writer, cpu_initial_apic_id());
  if (atomic_load(&panic_claimed) || !available) {
    display_end_frame();
    return false;
  }
  return true;
}

void display_copy(size_t offset, const void *pixels, size_t bytes)
{
  KASSERT(offset <= target->size && bytes <= target->size - offset);
  const uint8_t *source = pixels;
  while (bytes && !atomic_load(&panic_claimed)) {
    size_t count = bytes < DISPLAY_COPY_BYTES ? bytes : DISPLAY_COPY_BYTES;
    switch (driver) {
      case DISPLAY_BOOT:
        boot_display_copy(offset, source, count);
        break;
      case DISPLAY_VIRTIO_GPU:
        virtio_gpu_copy(offset, source, count);
        break;
    }
    offset += count;
    source += count;
    bytes -= count;
  }
}

void display_end_frame(void)
{
  if (driver == DISPLAY_VIRTIO_GPU && available && !display_is_panicking()) {
    available = virtio_gpu_present();
    failed = !available;
  }
  cpu_store_fence();
  atomic_store(&writer, DISPLAY_NO_WRITER);
}

const struct framebuffer *display_panic_target(void)
{
  atomic_store(&panic_claimed, true);
  if (driver == DISPLAY_VIRTIO_GPU) {
    return NULL;
  }
  uint32_t self = cpu_initial_apic_id();
  uint32_t rendering = atomic_load(&writer);
  if (rendering != self) {
    unsigned polls = 0;
    while (rendering != DISPLAY_NO_WRITER) {
      if (++polls > DISPLAY_PANIC_WAIT_LIMIT) {
        return NULL;
      }
      __asm__ volatile("pause");
      rendering = atomic_load(&writer);
    }
  }
  /* Drain an interrupted local writer before panic overwrites its pixels. */
  cpu_store_fence();
  return target;
}
