#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/audio.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "internal.h"

static struct hda_controller controller;
static struct hda_route route;
static struct task_wait *idle_wait;
static bool started, available;

static void audio_worker(void *argument);

void audio_require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(cpu_current() == cpu_bsp() && (flags & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(kernel_task_is_current(audio_worker, &controller));
  cpu_restore_interrupts(flags);
}

void audio_prepare(const struct boot_info *boot)
{
  hda_prepare(&controller, boot);
}

static void audio_worker(void *argument)
{
  (void)argument;
  audio_require_worker();
  if (!hda_link_start(&controller) || !hda_commands_start(&controller)) {
    hda_fail(&controller, "initialization failed");
    return;
  }
  if (!hda_codec_discover(&controller, &route)) {
    hda_fail(&controller, "no supported analog PCM route");
    return;
  }
  if (!hda_commands_stop(&controller)) {
    return;
  }
  available = true;
  klog("audio: ready codec=%x cad=%u pin=%u DAC=%u rate=%u format=%x; output idle\n",
      route.vendor, (unsigned)route.codec, (unsigned)route.pin,
      (unsigned)route.converter, HDA_RATE, HDA_STREAM_FORMAT);

  /* Retain a live BSP owner without periodic work. Session requests and refill
   * notification belong to later tasks; initialization never plays a tone. */
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    idle_wait = task_wait_prepare();
    task_wait_sleep(idle_wait);
    idle_wait = NULL;
    cpu_restore_interrupts(flags);
  }
}

void audio_start(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!started);
  started = true;
  if (!controller.prepared) {
    return;
  }
  if (kernel_task_create(audio_worker, &controller) != MM_OK) {
    klog("audio: worker unavailable; link remains reset and DMA disabled\n");
  }
}
