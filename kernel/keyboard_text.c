#include <kernel/keyboard.h>
#include <kernel/memory.h>
#include <kernel/string.h>

/* US key positions. Letters use Shift XOR Caps Lock; punctuation ignores Caps. */
static const struct {
  char plain, shifted;
} characters[KEY_COUNT] = {
  [KEY_A] = {'a', 'A'}, [KEY_B] = {'b', 'B'}, [KEY_C] = {'c', 'C'},
  [KEY_D] = {'d', 'D'}, [KEY_E] = {'e', 'E'}, [KEY_F] = {'f', 'F'},
  [KEY_G] = {'g', 'G'}, [KEY_H] = {'h', 'H'}, [KEY_I] = {'i', 'I'},
  [KEY_J] = {'j', 'J'}, [KEY_K] = {'k', 'K'}, [KEY_L] = {'l', 'L'},
  [KEY_M] = {'m', 'M'}, [KEY_N] = {'n', 'N'}, [KEY_O] = {'o', 'O'},
  [KEY_P] = {'p', 'P'}, [KEY_Q] = {'q', 'Q'}, [KEY_R] = {'r', 'R'},
  [KEY_S] = {'s', 'S'}, [KEY_T] = {'t', 'T'}, [KEY_U] = {'u', 'U'},
  [KEY_V] = {'v', 'V'}, [KEY_W] = {'w', 'W'}, [KEY_X] = {'x', 'X'},
  [KEY_Y] = {'y', 'Y'}, [KEY_Z] = {'z', 'Z'},
  [KEY_1] = {'1', '!'}, [KEY_2] = {'2', '@'}, [KEY_3] = {'3', '#'},
  [KEY_4] = {'4', '$'}, [KEY_5] = {'5', '%'}, [KEY_6] = {'6', '^'},
  [KEY_7] = {'7', '&'}, [KEY_8] = {'8', '*'}, [KEY_9] = {'9', '('},
  [KEY_0] = {'0', ')'}, [KEY_GRAVE] = {'`', '~'}, [KEY_MINUS] = {'-', '_'},
  [KEY_EQUAL] = {'=', '+'}, [KEY_LEFT_BRACKET] = {'[', '{'},
  [KEY_RIGHT_BRACKET] = {']', '}'}, [KEY_BACKSLASH] = {'\\', '|'},
  [KEY_SEMICOLON] = {';', ':'}, [KEY_APOSTROPHE] = {'\'', '"'},
  [KEY_COMMA] = {',', '<'}, [KEY_PERIOD] = {'.', '>'}, [KEY_SLASH] = {'/', '?'},
  [KEY_SPACE] = {' ', ' '}, [KEY_ENTER] = {'\n', '\n'},
  [KEY_KP_ENTER] = {'\n', '\n'}, [KEY_TAB] = {'\t', '\t'},
  [KEY_BACKSPACE] = {'\b', '\b'}, [KEY_ESCAPE] = {'\x1b', '\x1b'},
};

size_t keyboard_text(const struct key_event *event, char bytes[KEY_TEXT_MAX])
{
  if ((event->action != KEY_PRESS && event->action != KEY_REPEAT) ||
      event->key <= KEY_NONE || event->key >= KEY_COUNT ||
      (event->modifiers & (KEY_MOD_ALT | KEY_MOD_SUPER))) {
    return 0;
  }

  char plain = characters[event->key].plain;
  bool letter = plain >= 'a' && plain <= 'z';
  if (event->modifiers & KEY_MOD_CONTROL) {
    if (!letter) {
      return 0;
    }
    bytes[0] = plain - 'a' + 1;
    return 1;
  }
  if (plain) {
    bool shifted = (event->modifiers & KEY_MOD_SHIFT) != 0;
    if (letter && (event->modifiers & KEY_MOD_CAPS_LOCK)) {
      shifted = !shifted;
    }
    bytes[0] = shifted ? characters[event->key].shifted : plain;
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
