#ifndef PXE_KEY_LAYOUT_H
#define PXE_KEY_LAYOUT_H

#include <abi/key.h>

/* The US layout shared by the kernel's terminal text and userspace (libpyxis).
 * Returns the character a key position types under the given modifiers, or 0
 * when it types none. Letters follow Shift XOR Caps Lock; other main keys follow
 * Shift only. Keypad digits and decimal require Num Lock, independently of Shift;
 * keypad operators always give their characters. Enter, Tab, Backspace and Escape
 * give their control characters.
 * Control, Alt and Super are the caller's policy: they do not change the
 * result. */
char key_layout_character(enum key_code key, unsigned modifiers);

#endif
