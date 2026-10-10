#include <terminal/style.h>

static bool color_operand(const uint16_t *parameters, uint16_t present,
    size_t index, size_t count)
{
  return index < count && (present & (1u << index)) && parameters[index] <= 255;
}

bool terminal_sgr_apply(struct terminal_style *style, const uint16_t *parameters,
    uint16_t present, size_t count)
{
  if (!count || count > TERMINAL_CSI_PARAMETERS) {
    return false;
  }
  struct terminal_style next = *style;
  for (size_t i = 0; i < count; ++i) {
    unsigned parameter = parameters[i];
    if (parameter == 38 || parameter == 48 || parameter == 58) {
      size_t mode = i + 1;
      if (mode >= count || !(present & (1u << mode))) {
        return false;
      }
      terminal_color color;
      if (parameters[mode] == 5) {
        if (!color_operand(parameters, present, i + 2, count)) {
          return false;
        }
        color = parameters[i + 2];
        i += 2;
      } else if (parameters[mode] == 2) {
        if (!color_operand(parameters, present, i + 2, count) ||
            !color_operand(parameters, present, i + 3, count) ||
            !color_operand(parameters, present, i + 4, count)) {
          return false;
        }
        color = TERMINAL_COLOR_RGB | (uint32_t)parameters[i + 2] << 16 |
            (uint32_t)parameters[i + 3] << 8 | parameters[i + 4];
        i += 4;
      } else {
        return false;
      }
      if (parameter == 38) {
        next.foreground = color;
      } else if (parameter == 48) {
        next.background = color;
      }
      /* Unsupported underline colour still consumes its colour operands. */
    } else if (!parameter) {
      next = (struct terminal_style){
        .foreground = TERMINAL_COLOR_DEFAULT, .background = TERMINAL_COLOR_DEFAULT
      };
    } else if (parameter == 1 || parameter == 22) {
      if (parameter == 1) {
        next.attributes |= TERMINAL_ATTR_BOLD;
      } else {
        next.attributes &= ~TERMINAL_ATTR_BOLD;
      }
    } else if (parameter == 3 || parameter == 23) {
      if (parameter == 3) {
        next.attributes |= TERMINAL_ATTR_ITALIC;
      } else {
        next.attributes &= ~TERMINAL_ATTR_ITALIC;
      }
    } else if (parameter == 4 || parameter == 24) {
      if (parameter == 4) {
        next.attributes |= TERMINAL_ATTR_UNDERLINE;
      } else {
        next.attributes &= ~TERMINAL_ATTR_UNDERLINE;
      }
    } else if (parameter == 7 || parameter == 27) {
      if (parameter == 7) {
        next.attributes |= TERMINAL_ATTR_REVERSE;
      } else {
        next.attributes &= ~TERMINAL_ATTR_REVERSE;
      }
    } else if (parameter == 39) {
      next.foreground = TERMINAL_COLOR_DEFAULT;
    } else if (parameter == 49) {
      next.background = TERMINAL_COLOR_DEFAULT;
    } else if (parameter >= 30 && parameter <= 37) {
      next.foreground = parameter - 30;
    } else if (parameter >= 40 && parameter <= 47) {
      next.background = parameter - 40;
    } else if (parameter >= 90 && parameter <= 97) {
      next.foreground = parameter - 90 + 8;
    } else if (parameter >= 100 && parameter <= 107) {
      next.background = parameter - 100 + 8;
    }
  }
  *style = next;
  return true;
}

uint32_t terminal_color_rgb(terminal_color color, const uint32_t palette[16],
    uint32_t default_rgb)
{
  if (color < 16) {
    return palette[color];
  }
  if (color < 232) {
    static const uint8_t levels[] = {0, 95, 135, 175, 215, 255};
    unsigned cube = color - 16;
    return (uint32_t)levels[cube / 36] << 16 |
        (uint32_t)levels[cube / 6 % 6] << 8 | levels[cube % 6];
  }
  if (color < 256) {
    unsigned grey = 8 + 10 * (color - 232);
    return grey * UINT32_C(0x010101);
  }
  if ((color & UINT32_C(0xff000000)) == TERMINAL_COLOR_RGB) {
    return color & UINT32_C(0x00ffffff);
  }
  return default_rgb;
}
