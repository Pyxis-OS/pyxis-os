//
// Created by chronium on 9/18/26.
//

#include <kernel/mm/types.h>
#include <kernel/space.h>
#include <kernel/defs.h>
#include <arch/smp.h>
#include <kernel/string.h>
#include <kernel/format.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/types.h>
#include <kernel/panic.h>
#include <arch/cpu_local.h>

struct framebuffer *fb_alloc(struct boot_framebuffer *boot_fb)
{
  struct framebuffer *fb;
  fb = (struct framebuffer *)kmalloc(sizeof(struct framebuffer));

  fb->size = boot_fb->size;

  fb->width = boot_fb->width;
  fb->height = boot_fb->height;

  enum mm_result status = vm_alloc(vm_kernel_space(), 
      fb->size, PAGE_SIZE, PAGE_WRITE, &fb->address);

  if (status != MM_OK) {
    panic("cannot allocate space framebuffer (error %d)", (uint32_t)status);
  }

  return fb;
}

void space_init_all(struct boot_framebuffer *boot_fb)
{
  char *name;
  struct space *space;

  for (size_t i = 0; i < arch_cpu_count(); ++i) {
    if (i == 0) {
      name = strndup(KERNEL_NAME, strlen(KERNEL_NAME));
    } else {
      char buf[256];
      sprintf(buf, "CPU %ld", i);
      name = strndup(buf, strlen(buf));
    }

    klog("Initializing Space: %s\n", name);

    space = (struct space *)kmalloc(sizeof(struct space));

    space->name = name;
    space->fb = fb_alloc(boot_fb);

    arch_cpu_at(i)->space = space;
  }
}
