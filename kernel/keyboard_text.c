#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <pxe/key_layout.h>

/* The conventional (xterm, Linux console) control bytes for Ctrl with a
 * non-letter key. The character is the layout's, with Shift applied: Ctrl+@ and
 * Ctrl+Space give NUL, Ctrl+[ through Ctrl+_ give 0x1B..0x1F, and plain Ctrl+6
 * is also Ctrl+^. Any other character has no control byte. */
static bool control_punctuation(char character, char *byte)
{
  switch (character) {
  case '@':
  case ' ': *byte = 0x00; return true;
  case '[': *byte = 0x1b; return true;
  case '\\': *byte = 0x1c; return true;
  case ']': *byte = 0x1d; return true;
  case '^':
  case '6': *byte = 0x1e; return true;
  case '_': *byte = 0x1f; return true;
  default: return false;
  }
}

size_t keyboard_text(const struct key_event *event, char bytes[KEY_TEXT_MAX])
{
  if ((event->action != KEY_PRESS && event->action != KEY_REPEAT) ||
      event->key <= KEY_NONE || event->key >= KEY_COUNT ||
      (event->modifiers & (KEY_MOD_ALT | KEY_MOD_SUPER))) {
    return 0;
  }

  char plain = key_layout_character(event->key, 0);
  if (event->modifiers & KEY_MOD_CONTROL) {
    if (plain >= 'a' && plain <= 'z') {
      bytes[0] = plain - 'a' + 1;
      return 1;
    }
    /* Num Lock is withheld so keypad digits never become control bytes. */
    return control_punctuation(key_layout_character(event->key, event->modifiers & KEY_MOD_SHIFT),
        &bytes[0]) ? 1 : 0;
  }
  char character = key_layout_character(event->key, event->modifiers);
  if (character) {
    bytes[0] = character;
    return 1;
  }

  /* Modified navigation and function keys have no text binding yet. */
  if (event->modifiers & KEY_MOD_SHIFT) {
    return 0;
  }
  const char *sequence;
  switch (event->key) {
  case KEY_UP:
  case KEY_KP_8: sequence = "\x1b[A"; break;
  case KEY_DOWN:
  case KEY_KP_2: sequence = "\x1b[B"; break;
  case KEY_RIGHT:
  case KEY_KP_6: sequence = "\x1b[C"; break;
  case KEY_LEFT:
  case KEY_KP_4: sequence = "\x1b[D"; break;
  case KEY_HOME:
  case KEY_KP_7: sequence = "\x1b[H"; break;
  case KEY_END:
  case KEY_KP_1: sequence = "\x1b[F"; break;
  case KEY_DELETE:
  case KEY_KP_PERIOD: sequence = "\x1b[3~"; break;
  case KEY_PAGE_UP:
  case KEY_KP_9: sequence = "\x1b[5~"; break;
  case KEY_PAGE_DOWN:
  case KEY_KP_3: sequence = "\x1b[6~"; break;
  default: return 0;
  }
  size_t size = strlen(sequence);
  memcpy(bytes, sequence, size);
  return size;
}
