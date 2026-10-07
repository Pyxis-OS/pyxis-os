#include <arch/cpu.h>
#include <kernel/display.h>
#include <kernel/panic.h>
#include <stdatomic.h>
#include "boot.h"

#define DISPLAY_COPY_BYTES (64 * 1024)
#define DISPLAY_PANIC_WAIT_LIMIT 1000000
#define DISPLAY_NO_WRITER UINT32_MAX

static const struct framebuffer *target;
static atomic_bool panic_claimed;
/* Sequential consistency pairs writer publication/recheck with panic's
 * claim/load. Either the writer sees the claim or the claimant sees it. */
static _Atomic uint32_t writer = DISPLAY_NO_WRITER;

void display_init(const struct boot_framebuffer *boot)
{
  target = boot_display_init(boot);
}

const struct framebuffer *display_layout(void)
{
  return target;
}

bool display_begin_frame(void)
{
  atomic_store(&writer, cpu_initial_apic_id());
  if (atomic_load(&panic_claimed)) {
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
    boot_display_copy(offset, source, count);
    offset += count;
    source += count;
    bytes -= count;
  }
}

void display_end_frame(void)
{
  cpu_store_fence();
  atomic_store(&writer, DISPLAY_NO_WRITER);
}

const struct framebuffer *display_panic_target(void)
{
  atomic_store(&panic_claimed, true);
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
