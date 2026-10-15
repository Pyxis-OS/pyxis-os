#ifndef KERNEL_OBJECT_DESCRIBE_H
#define KERNEL_OBJECT_DESCRIBE_H

#include <abi/syscall.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Counted UTF-8, no embedded NUL and zeroed unused storage. */
bool description_string_valid(const char *text, size_t length, size_t capacity);

/* Provider-free current-process snapshot. No allocation or scheduling. */
struct syscall_result describe_call(uint64_t handle, uint64_t flags,
    uintptr_t destination);

#endif
