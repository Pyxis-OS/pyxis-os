#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/amd/renoir_flip.h>
#include <kernel/boot/options.h>
#include <kernel/display.h>
#include <kernel/display_capture.h>
#include <kernel/fb/early_console.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/memory.h>
#include <kernel/mm/vm.h>
#include <kernel/user/wait.h>
#include <stdatomic.h>
#include "boot.h"
#include "bochs.h"
#include "virtio_gpu.h"
#include "internal.h"
#include "timing.h"
#include "presentation.h"

#define DISPLAY_COPY_BYTES (64 * 1024)
#define DISPLAY_PANIC_WAIT_LIMIT 1000000
#define DISPLAY_NO_WRITER UINT32_MAX

enum display_driver { DISPLAY_BOOT, DISPLAY_VIRTIO_GPU, DISPLAY_BOCHS };
static enum display_driver driver;
static const struct framebuffer *target;
static bool available, failed;
static atomic_bool panic_claimed;
static atomic_bool direct_disabled;
/* Sequential consistency pairs writer publication/recheck with panic's
 * claim/load. Either the writer sees the claim or the claimant sees it. */
static _Atomic uint32_t writer = DISPLAY_NO_WRITER;
/* Boot and Bochs frames compose into this write-back copy of the front
 * buffer's layout; the frame end copies it to scanout memory in one pass.
 * Zero means composition writes the front buffer directly. Fixed for the
 * boot: these drivers never resize. VirtIO composes into its own RAM backing. */
static uintptr_t staging;
static bool flip_enabled, flip_pending;
static atomic_bool flip_failed;
/* Prepared before AP startup and retained until reboot. Panic never queries
 * the GPU, including while normal completion polling is interrupted. */
static const struct framebuffer *panic_surfaces[2];

/* Runtime availability is BSP-owned. Publish failure after backend waits and
 * outside rendering locks, then wake observers before restoring interrupts. */
static void set_availability(bool ready)
{
  uint64_t flags = cpu_save_interrupts();
  bool newly_failed = !ready && !failed;
  available = ready;
  failed = !ready;
  if (newly_failed) {
    readiness_notify();
  }
  cpu_restore_interrupts(flags);
}

bool display_modeset_begin(void)
{
  atomic_store(&direct_disabled, true);
  bool locked = log_begin();
  bool retired = locked && early_console_retire();
  log_end(locked);
  return retired && !display_is_panicking();
}

void display_init(const struct boot_info *boot, const char *size, const char *timing,
                  bool timing_metrics)
{
  if (boot->framebuffer.size) {
    target = boot_display_init(&boot->framebuffer);
  }
  struct pci_device *selected = NULL;
  /* Discovery prepends records; its first supported device is the last match. */
  for (const struct pci_device *device = pci_device_at(0); device; device = device->next) {
    if (virtio_gpu_matches(device) || bochs_display_matches(device)) {
      selected = (struct pci_device *)device;
    }
  }
  if (selected && virtio_gpu_matches(selected)) {
    driver = DISPLAY_VIRTIO_GPU;
    const struct framebuffer *gpu = display_modeset_begin() ?
        virtio_gpu_prepare(boot, selected) : NULL;
    failed = !gpu;
    if (gpu) {
      target = gpu;
    }
  } else if (selected) {
    const struct framebuffer *bochs = bochs_display_prepare(boot, selected, size);
    if (bochs) {
      driver = DISPLAY_BOCHS;
      target = bochs;
    }
    /* Refusal leaves the firmware mode intact or verifies its restoration. */
    atomic_store(&direct_disabled, false);
  } else if (size) {
    klog("display: no supported mode-setting device; keeping boot framebuffer\n");
  }
  if (!target) {
    panic("no usable display: firmware framebuffer or supported driver required");
  }
  panic_surfaces[0] = target;
  const struct boot_options *options = boot_options_get();
  if (driver == DISPLAY_BOOT && options->display_flip) {
    flip_enabled = renoir_flip_prepare(boot, target, options->display_flip_metrics);
    if (flip_enabled) {
      panic_surfaces[1] = renoir_flip_surface(1);
    }
  } else if (options->display_flip) {
    klog("renoir-flip: unavailable: not a firmware framebuffer backend\n");
  }
  if (!flip_enabled) {
    display_timing_prepare(boot, driver == DISPLAY_BOOT, timing, timing_metrics);
  }
}

/* BSP presenter. A frame without staging still presents, directly. */
static void allocate_staging(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (vm_alloc(vm_kernel_space(), target->size, PAGE_SIZE, PAGE_WRITE, &staging) != MM_OK) {
    staging = 0;
    klog("display: no %zu-byte staging frame; composing on the front buffer\n",
        target->size);
  }
  cpu_restore_interrupts(flags);
}

bool display_start(void)
{
  bool ready = false;
  switch (driver) {
    case DISPLAY_BOOT:
    case DISPLAY_BOCHS:
      allocate_staging();
      if (flip_enabled && !staging) {
        /* Qualification alone grants no submission without a complete staged
         * image. Cancel before the first GPU write; keep backing pinned. */
        renoir_flip_cancel_prepare();
        flip_enabled = false;
      }
      ready = true;
      break;
    case DISPLAY_VIRTIO_GPU:
      ready = virtio_gpu_start();
      break;
  }
  set_availability(ready);
  if (ready && driver == DISPLAY_BOOT && staging && !flip_enabled) {
    display_timing_start();
  }
  return ready;
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

static void update_gpu_availability(void)
{
  set_availability(virtio_gpu_available());
}

const struct framebuffer *display_resize_prepare(void)
{
  if (driver != DISPLAY_VIRTIO_GPU || !display_available()) {
    return NULL;
  }
  KASSERT(atomic_load(&writer) == DISPLAY_NO_WRITER);
  const struct framebuffer *candidate = virtio_gpu_resize_prepare();
  update_gpu_availability();
  return candidate;
}

bool display_resize_switch(void)
{
  KASSERT(driver == DISPLAY_VIRTIO_GPU && atomic_load(&writer) == DISPLAY_NO_WRITER);
  bool switched = virtio_gpu_resize_switch();
  update_gpu_availability();
  return switched;
}

bool display_resize_cancel(void)
{
  KASSERT(driver == DISPLAY_VIRTIO_GPU && atomic_load(&writer) == DISPLAY_NO_WRITER);
  bool cancelled = virtio_gpu_resize_cancel();
  update_gpu_availability();
  return cancelled;
}

bool display_resize_defer(void)
{
  KASSERT(driver == DISPLAY_VIRTIO_GPU && atomic_load(&writer) == DISPLAY_NO_WRITER);
  bool cancelled = virtio_gpu_resize_defer();
  update_gpu_availability();
  return cancelled;
}

void display_resize_commit(void)
{
  KASSERT(driver == DISPLAY_VIRTIO_GPU && atomic_load(&writer) == DISPLAY_NO_WRITER);
  virtio_gpu_resize_commit();
}

void display_resize_finish(void)
{
  KASSERT(driver == DISPLAY_VIRTIO_GPU && atomic_load(&writer) == DISPLAY_NO_WRITER);
  virtio_gpu_resize_finish();
  update_gpu_availability();
}

void display_resize_disable(void)
{
  if (driver == DISPLAY_VIRTIO_GPU) {
    virtio_gpu_resize_disable();
  }
}

bool display_begin_frame(void)
{
  KASSERT(!flip_pending);
  atomic_store(&writer, cpu_initial_apic_id());
  if (atomic_load(&panic_claimed) || !available) {
    display_end_frame(NULL);
    return false;
  }
  return true;
}

bool display_pointer_hardware(void)
{
  return driver == DISPLAY_VIRTIO_GPU;
}

void display_copy(size_t offset, const void *pixels, size_t bytes)
{
  KASSERT(offset <= target->size && bytes <= target->size - offset);
  const uint8_t *source = pixels;
  while (bytes && !atomic_load(&panic_claimed)) {
    size_t count = bytes < DISPLAY_COPY_BYTES ? bytes : DISPLAY_COPY_BYTES;
    switch (driver) {
      case DISPLAY_BOOT:
      case DISPLAY_BOCHS:
        if (staging) {
          memcpy((void *)(staging + offset), source, count);
        } else if (driver == DISPLAY_BOOT) {
          boot_display_copy(offset, source, count);
        } else {
          memcpy((void *)(target->address + offset), source, count);
        }
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

/* Copies the visible rows of a complete staged frame to scanout memory,
 * leaving row padding untouched. A panic claim stops it between chunks. */
static size_t copy_first_pixel(void)
{
  if (!display_timing_front_write()) {
    return 0;
  }
  uint32_t pixel;
  memcpy(&pixel, (const void *)staging, sizeof(pixel));
  /* Only the timestamp and one pixel store are IRQ-atomic. All observation,
   * waiting and the remaining copy run IF=1; no duplicated front write. */
  uint64_t flags = cpu_save_interrupts();
  uint64_t stamp = arch_monotonic_ns();
  __builtin_memcpy((void *)target->address, &pixel, sizeof(pixel));
  cpu_restore_interrupts(flags);
  display_timing_write_stamp(stamp);
  return sizeof(pixel);
}

static void copy_staging(void)
{
  size_t row_bytes = target->width * sizeof(uint32_t);
  if (target->pitch == row_bytes) {
    size_t total = row_bytes * target->height;
    for (size_t offset = 0; offset < total && !atomic_load(&panic_claimed);
        offset += DISPLAY_COPY_BYTES) {
      size_t count = MIN(total - offset, DISPLAY_COPY_BYTES);
      size_t prefix = offset ? 0 : copy_first_pixel();
      memcpy((void *)(target->address + offset + prefix),
          (const void *)(staging + offset + prefix), count - prefix);
      display_timing_progress((offset + count) / row_bytes);
    }
    return;
  }
  for (size_t y = 0; y < target->height && !atomic_load(&panic_claimed); ++y) {
    size_t offset = y * target->pitch;
    size_t prefix = y ? 0 : copy_first_pixel();
    memcpy((void *)(target->address + offset + prefix),
        (const void *)(staging + offset + prefix), row_bytes - prefix);
    if ((y + 1) % 8 == 0 || y + 1 == target->height) {
      display_timing_progress(y + 1);
    }
  }
}

static void copy_flip_surface(const struct framebuffer *surface)
{
  size_t row_bytes = target->width * sizeof(uint32_t);
  for (size_t y = 0; y < target->height && !display_is_panicking(); ++y) {
    for (size_t x = 0; x < row_bytes && !display_is_panicking();
        x += DISPLAY_COPY_BYTES) {
      size_t count = MIN(row_bytes - x, DISPLAY_COPY_BYTES);
      memcpy((void *)(surface->address + y * surface->pitch + x),
          (const void *)(staging + y * target->pitch + x), count);
    }
  }
}

/* Read-only validation/polling runs without the writer. A timeout retains
 * both possible fronts and copies the same immutable composite into each. */
static bool finish_flip(enum renoir_flip_state state)
{
  if (state == RENOIR_FLIP_FAILED || state == RENOIR_FLIP_OFF) {
    atomic_store(&flip_failed, true);
    set_availability(false);
    return false;
  }
  if (display_is_panicking()) {
    return false;
  }
  if (state == RENOIR_FLIP_READY) {
    return true;
  }
  if (state == RENOIR_FLIP_FALLBACK) {
    atomic_store(&writer, cpu_initial_apic_id());
    if (!display_is_panicking()) {
      copy_flip_surface(panic_surfaces[0]);
      copy_flip_surface(panic_surfaces[1]);
    }
    cpu_store_fence();
    atomic_store(&writer, DISPLAY_NO_WRITER);
    return !display_is_panicking();
  }
  return false;
}

bool display_frame_pending(void)
{
  return flip_pending;
}

bool display_frame_poll(void)
{
  KASSERT(flip_pending && atomic_load(&writer) == DISPLAY_NO_WRITER);
  if (!display_available()) {
    flip_pending = false;
    return false;
  }
  enum renoir_flip_state state = renoir_flip_poll();
  flip_pending = state == RENOIR_FLIP_PENDING && !display_is_panicking();
  return finish_flip(state);
}

static bool end_flip_frame(const struct pointer_frame *frame)
{
  enum renoir_flip_state state = renoir_flip_state();
  if (frame && available && !display_is_panicking() && state == RENOIR_FLIP_READY) {
    /* Prove that the back remains free before its first store, then reclaim
     * the direct writer. Submission also revalidates after the fenced copy. */
    cpu_store_fence();
    atomic_store(&writer, DISPLAY_NO_WRITER);
    state = renoir_flip_poll();
    atomic_store(&writer, cpu_initial_apic_id());
    if (state == RENOIR_FLIP_READY && !display_is_panicking()) {
      const struct framebuffer *back = renoir_flip_back();
      KASSERT(back);
      copy_flip_surface(back);
      cpu_store_fence();
      if (!display_is_panicking()) {
        state = renoir_flip_submit();
      }
    }
  }
  if (state == RENOIR_FLIP_FAILED || state == RENOIR_FLIP_OFF) {
    atomic_store(&flip_failed, true);
  }
  cpu_store_fence();
  atomic_store(&writer, DISPLAY_NO_WRITER);
  if (state == RENOIR_FLIP_FAILED || state == RENOIR_FLIP_OFF) {
    return finish_flip(state);
  }
  if (!frame || !available || display_is_panicking()) {
    return false;
  }
  if (state == RENOIR_FLIP_FALLBACK) {
    state = renoir_flip_poll();
  }
  flip_pending = state == RENOIR_FLIP_PENDING;
  /* Submission alone cannot confirm that the staged image became visible. */
  if (state == RENOIR_FLIP_READY) {
    return false;
  }
  return finish_flip(state);
}

bool display_end_frame(const struct pointer_frame *frame)
{
  if (flip_enabled) {
    return end_flip_frame(frame);
  }
  bool ready = available;
  if (staging && available && !display_is_panicking()) {
    /* Publish/recheck before any direct write, just as at frame begin. */
    atomic_store(&writer, DISPLAY_NO_WRITER);
    display_timing_begin_copy();
    atomic_store(&writer, cpu_initial_apic_id());
    if (!display_is_panicking()) {
      copy_staging();
    }
  }
  if (driver == DISPLAY_VIRTIO_GPU && available && !display_is_panicking()) {
    ready = virtio_gpu_present();
    if (ready && frame && !display_is_panicking()) {
      ready = virtio_gpu_pointer_present(frame, screen_capture_active());
    }
  }
  cpu_store_fence();
  atomic_store(&writer, DISPLAY_NO_WRITER);
  display_timing_finish();
  if (ready != available) {
    set_availability(ready);
  }
  return ready && !display_is_panicking();
}

const struct framebuffer *display_panic_target(void)
{
  atomic_store(&panic_claimed, true);
  if (driver == DISPLAY_VIRTIO_GPU || atomic_load(&direct_disabled) ||
      atomic_load(&flip_failed)) {
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
  return atomic_load(&flip_failed) ? NULL : target;
}

const struct framebuffer *display_panic_surface(unsigned index)
{
  return index < 2 ? panic_surfaces[index] : NULL;
}
