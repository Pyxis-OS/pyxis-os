#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <pxe/key_layout.h>

size_t keyboard_text(const struct key_event *event, char bytes[KEY_TEXT_MAX])
{
  if ((event->action != KEY_PRESS && event->action != KEY_REPEAT) ||
      event->key <= KEY_NONE || event->key >= KEY_COUNT ||
      (event->modifiers & (KEY_MOD_ALT | KEY_MOD_SUPER))) {
    return 0;
  }

  char plain = key_layout_character(event->key, 0);
  bool letter = plain >= 'a' && plain <= 'z';
  if (event->modifiers & KEY_MOD_CONTROL) {
    if (!letter) {
      return 0;
    }
    bytes[0] = plain - 'a' + 1;
    return 1;
  }
  if (plain) {
    bytes[0] = key_layout_character(event->key, event->modifiers);
    return 1;
  }

  /* Modified navigation and function/keypad keys have no text binding yet. */
  if (event->modifiers & KEY_MOD_SHIFT) {
    return 0;
  }
  const char *sequence;
  switch (event->key) {
  case KEY_UP: sequence = "\x1b[A"; break;
  case KEY_DOWN: sequence = "\x1b[B"; break;
  case KEY_RIGHT: sequence = "\x1b[C"; break;
  case KEY_LEFT: sequence = "\x1b[D"; break;
  case KEY_HOME: sequence = "\x1b[H"; break;
  case KEY_END: sequence = "\x1b[F"; break;
  case KEY_DELETE: sequence = "\x1b[3~"; break;
  case KEY_PAGE_UP: sequence = "\x1b[5~"; break;
  case KEY_PAGE_DOWN: sequence = "\x1b[6~"; break;
  default: return 0;
  }
  size_t size = strlen(sequence);
  memcpy(bytes, sequence, size);
  return size;
}
