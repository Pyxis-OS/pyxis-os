#ifndef ARCH_APIC_H
#define ARCH_APIC_H

#include <stdint.h>

#define APIC_TIMER_VECTOR 32
#define APIC_KEYBOARD_VECTOR 33
#define APIC_VIRTIO_FS_VECTOR 34
#define APIC_VIRTIO_NET_VECTOR 35
#define APIC_SPURIOUS_VECTOR 255

struct apic_msi_message {
  uint32_t address_low, address_high, data;
};

/* Physical xAPIC destination, fixed edge-triggered delivery to the BSP.
 * The caller must own a statically assigned external interrupt vector. */
struct apic_msi_message apic_bsp_msi_message(uint8_t vector);

/* xAPIC mode. Paging maps the device before setup. PIT calibration is shared:
 * initialize one CPU at a time, with IF=0, and leave the timer CPU-local. */
uint64_t apic_physical_address(void);
uint32_t apic_id(void);
void apic_init(void);
void apic_timer_start(void);
uint32_t apic_timer_remaining(void);
void apic_end_interrupt(void);

#endif
