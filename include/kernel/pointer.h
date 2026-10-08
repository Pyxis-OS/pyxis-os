#ifndef KERNEL_POINTER_H
#define KERNEL_POINTER_H

#include <abi/pointer.h>
#include <abi/syscall.h>
#include <stdbool.h>
#include <stddef.h>

struct space;
struct pointer_object;
struct pointer_image;
struct mouse_event;

/* Immutable image lease, retained until the matching frame has finished.
 * Static kernel defaults have no image reference. Coordinates are physical. */
struct pointer_frame {
  int64_t x, y;
  const uint8_t *pixels;
  uint32_t width, height, hotspot_x, hotspot_y;
  bool visible;
  struct pointer_image *image;
};

struct pointer_image {
  size_t references; /* BSP-only, including the published owner and frame leases. */
  uint32_t width, height, hotspot_x, hotspot_y;
  uint8_t pixels[];
};

/* BSP/IF=0. No allocation in routing/snapshot. */
void pointer_init(void);
void pointer_handle_input(const struct mouse_event *event);
void pointer_space_changed(struct space *space);
void pointer_geometry_changed(struct space *space);
void pointer_surface_ended(struct space *space);
void pointer_subscription_started(struct pointer_object *pointer);
void pointer_subscription_ended(struct pointer_object *pointer);
void pointer_frame_snapshot(struct pointer_frame *frame);
void pointer_frame_release(struct pointer_frame *frame);
void pointer_image_release(struct pointer_image *image);
struct pointer_event pointer_position_event(struct pointer_object *pointer, uint32_t type);
struct pointer_geometry pointer_surface_geometry(struct pointer_object *pointer);
bool pointer_surface_focused(struct pointer_object *pointer);
enum call_status pointer_surface_warp(struct pointer_object *pointer,
    const struct pointer_warp_request *request);

#endif
