#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H
[[noreturn]] void panic(const char *format, ...)
  __attribute__((format(printf, 1, 2)));
#define KASSERT(condition) \
  ((condition) ? (void)0 : panic("assertion: %s (%s:%d)", #condition, __FILE__, __LINE__))
#endif
