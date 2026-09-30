#ifndef REMOTE_BUFFER_H
#define REMOTE_BUFFER_H

#include <stddef.h>
#include <string.h>

struct byte_buffer {
  unsigned char *data;
  size_t capacity;
  size_t start;
  size_t length;
};

static inline size_t buffer_space(const struct byte_buffer *buffer)
{
  return buffer->capacity - buffer->length;
}

static inline void buffer_append(struct byte_buffer *buffer, const void *data, size_t length)
{
  if (buffer->start + buffer->length + length > buffer->capacity) {
    memmove(buffer->data, buffer->data + buffer->start, buffer->length);
    buffer->start = 0;
  }
  memcpy(buffer->data + buffer->start + buffer->length, data, length);
  buffer->length += length;
}

static inline void buffer_consume(struct byte_buffer *buffer, size_t length)
{
  buffer->start += length;
  buffer->length -= length;
  if (!buffer->length) {
    buffer->start = 0;
  }
}

#endif
