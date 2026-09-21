#include <arch/smp.h>
#include <kernel/console.h>
#include <kernel/fb/tty.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>

static void destroy_console(struct kernel_object *object)
{
  /* The reference header is the first member; only the wrapper is owned. */
  kfree((struct console_object *)object);
}

struct console_object *console_create(struct tty *tty)
{
  KASSERT(arch_cpu_index() == 0 && tty && tty->initialized);
  struct console_object *console = kmalloc(sizeof(*console));
  if (!console) {
    return NULL;
  }
  object_init(&console->object, OBJECT_CONSOLE, destroy_console);
  console->tty = tty;
  return console;
}

bool console_write(struct console_object *console, const char *bytes, size_t size)
{
  bool locked = log_begin();
  if (!locked) {
    return false;
  }
  if (!console->tty->initialized) {
    log_end(locked);
    return false;
  }

  for (size_t i = 0; i < size; ++i) {
    tty_put_char(console->tty, bytes[i]);
  }
  log_end(locked);
  return true;
}
