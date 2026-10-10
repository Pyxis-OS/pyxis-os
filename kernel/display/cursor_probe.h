#ifndef KERNEL_DISPLAY_CURSOR_PROBE_H
#define KERNEL_DISPLAY_CURSOR_PROBE_H

#include <kernel/pointer.h>

void cursor_probe_begin(void);
void cursor_probe_compose(void);
void cursor_probe_copy(void);
void cursor_probe_pointer(const struct pointer_frame *frame);
void cursor_probe_record(void);
void cursor_probe_service(uint64_t frames, uint64_t compose_total, uint64_t copy_total);
void cursor_probe_skip(void);

#endif
