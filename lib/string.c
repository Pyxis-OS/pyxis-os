#include <kernel/string.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <stdint.h>

size_t strlen(const char *text)
{
  size_t length = 0;
  while (text[length]) {
    ++length;
  }
  return length;
}

size_t strnlen(const char *text, size_t max_length)
{
  size_t length = 0;
  while (length < max_length && text[length]) {
    ++length;
  }
  return length;
}

char *strndup(const char *text, size_t max_length)
{
  size_t length = strnlen(text, max_length);
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
