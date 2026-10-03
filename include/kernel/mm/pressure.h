#ifndef KERNEL_MM_PRESSURE_H
#define KERNEL_MM_PRESSURE_H

#include <stdbool.h>

struct task_wait;

/* BSP/IF=0, allocation-free notification. It never reclaims synchronously or
 * changes an allocation's result. The sole cache worker drains notifications
 * and registers its borrowed wait only while asleep; detach before reuse. */
void mm_pressure_notify(void);
bool mm_pressure_take(void);
void mm_pressure_wait(struct task_wait *wait);

#endif
