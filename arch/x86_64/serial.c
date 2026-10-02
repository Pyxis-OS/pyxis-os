#include <arch/console.h>
#include <arch/cpu.h>
#include <stdatomic.h>

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
#define UART_MCR_LOOPBACK (1u << 4)
#define UART_LSR_DATA_READY (1u << 0)
#define UART_LSR_TRANSMIT_EMPTY (1u << 5)
#define UART_DIVISOR_115200 1
#define UART_LOOPBACK_TEST_BYTE 0xae
/* No clock exists this early. One character at 115200 baud is about 87 us and
 * a port read takes roughly 1 us, so this allows tens of milliseconds. */
#define UART_POLL_LIMIT 100000

/* Latched false when the port is absent or stops accepting output; panics
 * transmit without the log lock. A recovered port is not retried. */
static atomic_bool serial_available;

static bool poll_line_status(uint8_t bit)
{
  for (unsigned i = 0; i < UART_POLL_LIMIT; ++i) {
    if (inb(COM1_BASE + UART_LINE_STATUS) & bit) {
      return true;
    }
    __asm__ volatile("pause");
  }
  return false;
}

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

  /* Loopback output is not transmitted. A missing port reads as 0xff. */
  outb(COM1_BASE + UART_MODEM_CONTROL,
       UART_MCR_DATA_TERMINAL_READY | UART_MCR_REQUEST_TO_SEND | UART_MCR_LOOPBACK);
  outb(COM1_BASE + UART_DATA, UART_LOOPBACK_TEST_BYTE);
  bool present = poll_line_status(UART_LSR_DATA_READY) &&
      inb(COM1_BASE + UART_DATA) == UART_LOOPBACK_TEST_BYTE;
  outb(COM1_BASE + UART_MODEM_CONTROL,
       UART_MCR_DATA_TERMINAL_READY | UART_MCR_REQUEST_TO_SEND);
  atomic_store_explicit(&serial_available, present, memory_order_relaxed);
}

void console_putc(char ch)
{
  if (!atomic_load_explicit(&serial_available, memory_order_relaxed)) {
    return;
  }
  if (ch == '\n') {
    console_putc('\r');
  }
  if (!poll_line_status(UART_LSR_TRANSMIT_EMPTY)) {
    atomic_store_explicit(&serial_available, false, memory_order_relaxed);
    return;
  }
  outb(COM1_BASE + UART_DATA, (uint8_t)ch);
}

int console_getc(void)
{
  if (!atomic_load_explicit(&serial_available, memory_order_relaxed) ||
      !(inb(COM1_BASE + UART_LINE_STATUS) & UART_LSR_DATA_READY)) {
    return -1;
  }
  return inb(COM1_BASE + UART_DATA);
}
