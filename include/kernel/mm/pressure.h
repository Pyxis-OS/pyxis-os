#ifndef KERNEL_MM_PRESSURE_H
#define KERNEL_MM_PRESSURE_H

#include <stdbool.h>

struct task_wait;

/* Any CPU with IF=0, allocation-free notification. It never reclaims
 * synchronously or changes an allocation's result. Callers must not hold the
 * queue lock. The sole cache worker drains notifications and registers its
 * prepared wait only around its sleep; registering after a missed notification
 * wakes it at once. Detach with NULL before reusing the wait. */
void mm_pressure_notify(void);
bool mm_pressure_take(void);
void mm_pressure_wait(struct task_wait *wait);

#endif
