#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <arch/cpu.h>
#include <kernel/panic.h>
#include <stdatomic.h>

/* A short busy-wait lock shared between CPUs. Every holder runs with IF=0, so
 * interrupt and fault entry must never take one. Never hold a spinlock across
 * logging, a context switch or waiting for another CPU, and never take the
 * same lock twice. Only the heap growth lock may be held across allocation.
 * Each lock documents what it may nest inside. */
struct spinlock {
  atomic_bool locked;
};

static inline void spin_lock(struct spinlock *lock)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&lock->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static inline void spin_unlock(struct spinlock *lock)
{
  atomic_store_explicit(&lock->locked, false, memory_order_release);
}

#endif
