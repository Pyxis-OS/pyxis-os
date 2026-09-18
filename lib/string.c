#include <kernel/string.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <stdint.h>

char *strndup(const char *text, size_t max_length)
{
  size_t length = 0;
  while (length < max_length && text[length]) {
    ++length;
  }
  if (length == SIZE_MAX) {
    return NULL;
  }

  char *copy = kmalloc(length + 1);
  if (!copy) {
    return NULL;
  }

  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}
