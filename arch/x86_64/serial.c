#include <arch/console.h>
#include <arch/cpu.h>

#define COM1 0x3f8

void serial_init(void)
{
  outb(COM1 + 1, 0);
  outb(COM1 + 3, 0x80);
  outb(COM1, 1);
  outb(COM1 + 1, 0);
  outb(COM1 + 3, 3);
  outb(COM1 + 2, 0xc7);
  outb(COM1 + 4, 3);
}

void console_putc(char ch)
{
  if (ch == '\n') {
    console_putc('\r');
  }
  while (!(inb(COM1 + 5) & 0x20)) {
    __asm__ volatile("pause");
  }
  outb(COM1, (uint8_t)ch);
}

int console_getc(void)
{
  return (inb(COM1 + 5) & 1) ? inb(COM1) : -1;
}
