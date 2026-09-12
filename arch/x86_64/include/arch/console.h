#ifndef ARCH_CONSOLE_H
#define ARCH_CONSOLE_H
void serial_init(void);
void console_putc(char ch);
/* Nonblocking polling read: -1 when no character is available. */
int console_getc(void);
#endif
