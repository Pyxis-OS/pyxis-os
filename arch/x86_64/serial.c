#include <arch/console.h>
#include <arch/cpu.h>

#define COM1_BASE 0x3f8
#define UART_DATA 0
#define UART_INTERRUPT_ENABLE 1
#define UART_FIFO_CONTROL 2
#define UART_LINE_CONTROL 3
#define UART_MODEM_CONTROL 4
#define UART_LINE_STATUS 5
#define UART_DIVISOR_LOW 0
#define UART_DIVISOR_HIGH 1

#define UART_LCR_8_BITS 3
#define UART_LCR_DIVISOR_ACCESS (1u << 7)
#define UART_FCR_ENABLE (1u << 0)
#define UART_FCR_CLEAR_RECEIVE (1u << 1)
#define UART_FCR_CLEAR_TRANSMIT (1u << 2)
#define UART_FCR_TRIGGER_14_BYTES (3u << 6)
#define UART_MCR_DATA_TERMINAL_READY (1u << 0)
#define UART_MCR_REQUEST_TO_SEND (1u << 1)
#define UART_LSR_DATA_READY (1u << 0)
#define UART_LSR_TRANSMIT_EMPTY (1u << 5)
#define UART_DIVISOR_115200 1

void serial_init(void)
{
  outb(COM1_BASE + UART_INTERRUPT_ENABLE, 0);
  /* DLAB temporarily replaces the data and interrupt-enable registers with
   * the baud divisor. Clear it before polling; 8 bits, no parity, one stop. */
  outb(COM1_BASE + UART_LINE_CONTROL, UART_LCR_DIVISOR_ACCESS);
  outb(COM1_BASE + UART_DIVISOR_LOW, UART_DIVISOR_115200);
  outb(COM1_BASE + UART_DIVISOR_HIGH, 0);
  outb(COM1_BASE + UART_LINE_CONTROL, UART_LCR_8_BITS);
  outb(COM1_BASE + UART_FIFO_CONTROL,
       UART_FCR_ENABLE | UART_FCR_CLEAR_RECEIVE | UART_FCR_CLEAR_TRANSMIT |
       UART_FCR_TRIGGER_14_BYTES);
  outb(COM1_BASE + UART_MODEM_CONTROL,
       UART_MCR_DATA_TERMINAL_READY | UART_MCR_REQUEST_TO_SEND);
}

void console_putc(char ch)
{
  if (ch == '\n') {
    console_putc('\r');
  }
  while (!(inb(COM1_BASE + UART_LINE_STATUS) & UART_LSR_TRANSMIT_EMPTY)) {
    __asm__ volatile("pause");
  }
  outb(COM1_BASE + UART_DATA, (uint8_t)ch);
}

int console_getc(void)
{
  if (!(inb(COM1_BASE + UART_LINE_STATUS) & UART_LSR_DATA_READY)) {
    return -1;
  }
  return inb(COM1_BASE + UART_DATA);
}
