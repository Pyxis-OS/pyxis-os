#ifndef KERNEL_DISPLAY_INTERNAL_H
#define KERNEL_DISPLAY_INTERNAL_H

/* Normal driver work stops on panic without submitting or resetting a device. */
bool display_is_panicking(void);

#endif
