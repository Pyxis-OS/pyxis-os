#ifndef ARCH_CONSOLE_H
#define ARCH_CONSOLE_H
/* Detects COM1 with a loopback test. Output to an absent port, or one that
 * stops accepting bytes within a bounded poll, is dropped without waiting. */
void serial_init(void);
void console_putc(char ch);
/* Nonblocking polling read: -1 when no character is available. */
int console_getc(void);
#endif
