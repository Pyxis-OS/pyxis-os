#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/boot/options.h>
#include <kernel/display.h>
#include <kernel/log.h>
#include <stdatomic.h>
#include "internal.h"

#define NS_PER_SECOND UINT64_C(1000000000)

/* All state except BLANKED belongs to the BSP at IF=0. The presenter reads
 * BLANKED at IF=1 when it begins a frame. */
static atomic_bool blanked;
static bool started, enabled;
/* A configured period of 0 blanks when presentation starts. That period has no
 * meaning for a woken screen, so later idle periods use the default. */
static bool blank_at_start;
static uint64_t idle_ns, last_input_ns;

void display_blank_start(void)
{
  const struct boot_options *options = boot_options_get();
  uint64_t flags = cpu_save_interrupts();
  uint32_t seconds = options->display_idle_blank_seconds;
  enabled = options->display_idle_blank;
  blank_at_start = enabled && !seconds;
  idle_ns = (seconds ? seconds : DISPLAY_IDLE_BLANK_DEFAULT_SECONDS) * NS_PER_SECOND;
  last_input_ns = arch_monotonic_ns();
  started = true;
  cpu_restore_interrupts(flags);
}

bool display_blanked(void)
{
  return atomic_load(&blanked);
}

bool display_input_activity(bool wakes)
{
  if (!started || !enabled) {
    return false;
  }
  last_input_ns = arch_monotonic_ns();
  blank_at_start = false;
  if (!wakes || !atomic_load(&blanked)) {
    return false;
  }
  atomic_store(&blanked, false);
  kinfo("display: woken by local input\n");
  return true;
}

bool display_blank_update(void)
{
  if (started && enabled && !atomic_load(&blanked) &&
      (blank_at_start || arch_monotonic_ns() - last_input_ns >= idle_ns)) {
    atomic_store(&blanked, true);
    kinfo("display: idle blank\n");
  }
  return atomic_load(&blanked);
}
