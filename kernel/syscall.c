#include <kernel/syscall.h>
#include <kernel/user.h>

#include "../include/kernel/fb/tty.h"
#include "../include/kernel/log.h"

int64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  (void)arg2;
  (void)arg3;
  (void)arg4;
  (void)arg5;
  (void)arg6;
  
  switch (number) {
  case SYSCALL_PUTCHAR: {
    bool locked = log_begin();
    if (locked && get_tty()->initialized) {
      tty_put_char(get_tty(), (char)arg1);
    }
    log_end(locked);
    return 0;
  }
  case SYSCALL_LOG_PUTCHAR: {
    bool locked = log_begin();
    log_putc((char)arg1);
    log_end(locked);
    return 0;
  }
  case SYSCALL_EXIT:
    user_exit((int32_t)(uint32_t)arg1);
  default:
    return -1;
  }
}
