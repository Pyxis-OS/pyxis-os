#include <arch/smp.h>
#include <kernel/display.h>
#include <kernel/mm/heap.h>
#include <kernel/mouse.h>
#include <kernel/object/display.h>
#include <kernel/object/pointer.h>
#include <kernel/panic.h>
#include <kernel/pointer.h>
#include <kernel/space.h>

_Static_assert(MOUSE_BUTTON_LEFT == POINTER_BUTTON_LEFT &&
    MOUSE_BUTTON_RIGHT == POINTER_BUTTON_RIGHT &&
    MOUSE_BUTTON_MIDDLE == POINTER_BUTTON_MIDDLE, "pointer button bits");

enum destination_kind { DESTINATION_NONE, DESTINATION_TERMINAL, DESTINATION_GRAPHICS };
struct pointer_destination {
  struct space *space;
  enum destination_kind kind;
};

static int64_t position_x, position_y;
static uint32_t device_buttons, consumed_buttons, drag_buttons;
static struct pointer_destination drag;
static struct pointer_object *hover;
static uint8_t arrow_pixels[16 * 24 * 4], terminal_pixels[9 * 20 * 4];

static void default_pixel(uint8_t *pixels, size_t index, bool white)
{
  uint8_t *pixel = pixels + index * 4;
  pixel[0] = pixel[1] = pixel[2] = white ? 255 : 0;
  pixel[3] = 255;
}

void pointer_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  const struct framebuffer *layout = display_layout();
  position_x = layout->width / 2;
  position_y = layout->height / 2;
  for (size_t y = 0; y < 17; ++y) {
    size_t width = y / 2 + 1;
    for (size_t x = 0; x < width; ++x) {
      default_pixel(arrow_pixels, y * 16 + x, x && x + 1 < width && y < 16);
    }
  }
  for (size_t y = 12; y < 23; ++y) {
    for (size_t x = 4; x < 8; ++x) {
      default_pixel(arrow_pixels, y * 16 + x, x == 5 || x == 6);
    }
  }
  for (size_t y = 0; y < 20; ++y) {
    for (size_t x = 0; x < 9; ++x) {
      if (y < 3 || y >= 17 || (x >= 3 && x <= 5)) {
        default_pixel(terminal_pixels, y * 9 + x,
            (y == 1 || y == 18) ? (x > 0 && x < 8) : x == 4);
      }
    }
  }
}

static int64_t clamped_move(int64_t position, int32_t delta, size_t extent)
{
  KASSERT(extent && extent <= INT64_MAX);
  int64_t high = extent - 1;
  if (delta > 0 && position > high - delta) {
    return high;
  }
  if (delta < 0 && position < -(int64_t)delta) {
    return 0;
  }
  return position + delta;
}

static void clamp_position(void)
{
  const struct framebuffer *layout = display_layout();
  position_x = clamped_move(position_x, 0, layout->width);
  position_y = clamped_move(position_y, 0, layout->height);
  if (position_x >= (int64_t)layout->width) {
    position_x = layout->width - 1;
  }
  if (position_y >= (int64_t)layout->height) {
    position_y = layout->height - 1;
  }
}

static struct pointer_destination hit_test(void)
{
  struct space *space = space_pointer_active();
  if (position_y < (int64_t)space_pointer_content_y()) {
    return (struct pointer_destination){0};
  }
  struct display_object *display = space->display;
  if (display->visible) {
    const struct framebuffer *mapping = &display->frame->fb;
    if (position_x < (int64_t)mapping->width &&
        position_y - (int64_t)space_pointer_content_y() < (int64_t)mapping->height) {
      return (struct pointer_destination){space, DESTINATION_GRAPHICS};
    }
    return (struct pointer_destination){0};
  }
  return (struct pointer_destination){space, DESTINATION_TERMINAL};
}

bool pointer_surface_focused(struct pointer_object *pointer)
{
  return pointer->space == space_pointer_active() && pointer->space->display->visible;
}

bool pointer_surface_hovered(struct pointer_object *pointer)
{
  struct pointer_destination target = hit_test();
  return target.kind == DESTINATION_GRAPHICS && target.space == pointer->space;
}

struct pointer_geometry pointer_surface_geometry(struct pointer_object *pointer)
{
  struct space *space = pointer->space;
  const struct display_object *display = space->display;
  return (struct pointer_geometry){
    .width = space->fb->width, .height = space->fb->height,
    .generation = space->tty->geometry_generation,
    .mapping_identity = display->mapping_identity,
    .mapping_width = display->frame ? display->frame->fb.width : 0,
    .mapping_height = display->frame ? display->frame->fb.height : 0,
  };
}

struct pointer_event pointer_position_event(struct pointer_object *pointer, uint32_t type)
{
  struct pointer_geometry geometry = pointer_surface_geometry(pointer);
  return (struct pointer_event){.x = position_x,
    .y = position_y - (int64_t)space_pointer_content_y(),
    .generation = geometry.generation, .mapping_identity = geometry.mapping_identity,
    .type = type};
}

static void update_hover(struct pointer_destination target)
{
  struct pointer_object *next = target.kind == DESTINATION_GRAPHICS ? target.space->pointer : NULL;
  if (drag.space || next == hover) {
    return;
  }
  if (hover) {
    pointer_queue_state(hover, POINTER_LEAVE);
  }
  hover = next;
  if (hover) {
    pointer_queue_state(hover, POINTER_ENTER);
  }
}

void pointer_subscription_ended(struct pointer_object *pointer)
{
  if (drag.space == pointer->space && drag.kind == DESTINATION_GRAPHICS) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  if (hover == pointer) {
    hover = NULL;
  }
}

void pointer_space_changed(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  bool focused = pointer_surface_focused(space->pointer);
  if (drag.space == space &&
      (space != space_pointer_active() ||
       (drag.kind == DESTINATION_GRAPHICS) != space->display->visible)) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  pointer_focus(space->pointer, focused);
  update_hover(hit_test());
}

void pointer_geometry_changed(struct space *space)
{
  KASSERT(arch_cpu_index() == 0);
  clamp_position();
  if (drag.space == space) {
    drag = (struct pointer_destination){0};
    drag_buttons = 0;
  }
  pointer_reset_input(space->pointer, POINTER_GEOMETRY_CHANGED);
  if (hover == space->pointer) {
    hover = NULL;
  }
  update_hover(hit_test());
}

void pointer_surface_ended(struct space *space)
{
  pointer_end_session(space->pointer);
}

void pointer_image_release(struct pointer_image *image)
{
  KASSERT(arch_cpu_index() == 0);
  if (image) {
    KASSERT(image->references);
    if (--image->references == 0) {
      kfree(image);
    }
  }
}

void pointer_frame_snapshot(struct pointer_frame *frame)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_destination target = hit_test();
  *frame = (struct pointer_frame){.x = position_x, .y = position_y,
    .pixels = arrow_pixels, .width = 16, .height = 24,
    .visible = mouse_available() && display_available()};
  if (target.kind == DESTINATION_TERMINAL && target.space != space_caelum()) {
    frame->pixels = terminal_pixels;
    frame->width = 9;
    frame->height = 20;
    frame->hotspot_x = 4;
    frame->hotspot_y = 10;
  } else if (target.kind == DESTINATION_GRAPHICS) {
    struct pointer_object *pointer = target.space->pointer;
    frame->visible = frame->visible && !pointer->hidden;
    struct pointer_image *image = pointer->image;
    if (image) {
      KASSERT(image->references && image->references < SIZE_MAX);
      ++image->references;
      frame->image = image;
      frame->pixels = image->pixels;
      frame->width = image->width;
      frame->height = image->height;
      frame->hotspot_x = image->hotspot_x;
      frame->hotspot_y = image->hotspot_y;
    }
  }
}

void pointer_frame_release(struct pointer_frame *frame)
{
  pointer_image_release(frame->image);
  *frame = (struct pointer_frame){0};
}

void pointer_handle_input(const struct mouse_event *event)
{
  KASSERT(arch_cpu_index() == 0);
  if (event->reset) {
    device_buttons = MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT | MOUSE_BUTTON_MIDDLE;
    consumed_buttons = drag_buttons = 0;
    drag = (struct pointer_destination){0};
    hover = NULL;
    for (struct space *space = space_caelum(); space; space = space->next) {
      pointer_reset_input(space->pointer, POINTER_STATE_RESET);
    }
    return;
  }
  const struct framebuffer *layout = display_layout();
  position_x = clamped_move(position_x, event->dx, layout->width);
  position_y = clamped_move(position_y, event->dy, layout->height);
  uint32_t pressed = event->buttons & ~device_buttons;
  device_buttons = event->buttons;
  consumed_buttons &= device_buttons;
  pressed &= ~consumed_buttons;
  struct pointer_destination target = hit_test();
  if (!drag.space && (pressed & POINTER_BUTTON_LEFT)) {
    struct space *tab = space_pointer_tab(position_x, position_y);
    if (tab) {
      consumed_buttons |= POINTER_BUTTON_LEFT;
      space_pointer_select(tab);
      return;
    }
  }
  update_hover(target);
  uint32_t buttons = device_buttons & ~consumed_buttons;
  if (!drag.space && pressed && target.space) {
    drag = target;
    drag_buttons = pressed & buttons;
  }
  struct pointer_destination recipient = drag.space ? drag : target;
  if (recipient.kind == DESTINATION_GRAPHICS &&
      (event->dx || event->dy || pressed || drag_buttons != (drag_buttons & buttons))) {
    struct pointer_object *pointer = recipient.space->pointer;
    struct pointer_event input = pointer_position_event(pointer, POINTER_INPUT);
    input.buttons = buttons;
    pointer_queue_input(pointer, input, pressed, true);
    if (drag.space) {
      drag_buttons = pointer->accepted;
    }
  } else if (drag.space) {
    drag_buttons = (drag_buttons & buttons) | (pressed & buttons);
  }
  if (event->wheel && target.kind == DESTINATION_GRAPHICS) {
    struct pointer_object *pointer = target.space->pointer;
    struct pointer_event input = pointer_position_event(pointer, POINTER_INPUT);
    input.wheel = event->wheel;
    pointer_queue_input(pointer, input, 0, false);
  }
  if (drag.space && !drag_buttons) {
    drag = (struct pointer_destination){0};
    update_hover(target);
  }
}

enum call_status pointer_surface_warp(struct pointer_object *pointer,
    const struct pointer_warp_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  struct pointer_geometry geometry = pointer_surface_geometry(pointer);
  if (request->generation != geometry.generation ||
      request->mapping_identity != geometry.mapping_identity) {
    return CALL_BUSY;
  }
  if (!pointer_surface_focused(pointer) ||
      (drag.space && (drag.space != pointer->space || drag.kind != DESTINATION_GRAPHICS))) {
    return CALL_DENIED;
  }
  uint64_t width = MIN(geometry.width, geometry.mapping_width);
  uint64_t height = MIN(geometry.height, geometry.mapping_height);
  if (request->x < 0 || request->y < 0 || (uint64_t)request->x >= width ||
      (uint64_t)request->y >= height) {
    return CALL_BAD_REQUEST;
  }
  position_x = request->x;
  position_y = request->y + (int64_t)space_pointer_content_y();
  update_hover(hit_test());
  pointer_queue_input(pointer, pointer_position_event(pointer, POINTER_INPUT), 0, false);
  return CALL_OK;
}
