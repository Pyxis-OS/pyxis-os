#include "paste.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool printable(unsigned char byte)
{
  return byte >= 0x20 && byte <= 0x7e;
}

static bool unquoted_literal(unsigned char byte)
{
  switch (byte) {
  case '$': case '`': case '*': case '?': case '[': case ']':
  case '{': case '}': case '(': case ')': case '<': case '>':
  case '|': case '&': case ';': case '!': case '#':
    return false;
  default:
    return true;
  }
}

static bool decode_word(const unsigned char *data, size_t length,
    char word[PASTE_PATH_CAPACITY])
{
  for (size_t i = 0; i < length; ++i) {
    if (!printable(data[i])) {
      return false;
    }
  }

  size_t position = 0;
  while (position < length && data[position] == ' ') {
    ++position;
  }

  size_t used = 0;
  unsigned char quote = 0;
  while (position < length) {
    unsigned char byte = data[position++];
    if (!quote && byte == ' ') {
      while (position < length && data[position] == ' ') {
        ++position;
      }
      if (position != length) {
        return false;
      }
      break;
    }
    if (quote && byte == quote) {
      quote = 0;
      continue;
    }
    if (!quote && (byte == '\'' || byte == '"')) {
      quote = byte;
      continue;
    }
    if (byte == '\\' && quote != '\'') {
      if (position == length) {
        return false;
      }
      byte = data[position++];
      if (quote == '"' && byte != '\\' && byte != '"' &&
          byte != '$' && byte != '`') {
        return false;
      }
    } else if (quote == '"' && (byte == '$' || byte == '`')) {
      return false;
    } else if (!quote && !unquoted_literal(byte)) {
      return false;
    }
    if (used == PASTE_PATH_CAPACITY - 1) {
      return false;
    }
    word[used++] = (char)byte;
  }
  if (quote || !used) {
    return false;
  }
  word[used] = '\0';
  return true;
}

bool paste_file_path(const unsigned char *data, size_t length,
    char path[PASTE_PATH_CAPACITY], char name[PASTE_NAME_CAPACITY])
{
  char word[PASTE_PATH_CAPACITY];
  if (!decode_word(data, length, word)) {
    return false;
  }

  const char *source = word;
  char absolute[PASTE_PATH_CAPACITY];
  if (!strncmp(word, "~/", 2)) {
    const char *home = getenv("HOME");
    if (!home || *home != '/') {
      return false;
    }
    size_t home_length = strlen(home);
    size_t tail_length = strlen(word + 1);
    if (home_length >= sizeof(absolute) ||
        tail_length >= sizeof(absolute) - home_length) {
      return false;
    }
    for (size_t i = 0; i < home_length; ++i) {
      if (!printable((unsigned char)home[i])) {
        return false;
      }
    }
    memcpy(absolute, home, home_length);
    memcpy(absolute + home_length, word + 1, tail_length + 1);
    source = absolute;
  } else if (*word != '/') {
    return false;
  }

  const char *base = strrchr(source, '/') + 1;
  size_t name_length = strlen(base);
  if (!name_length || name_length >= PASTE_NAME_CAPACITY ||
      !strcmp(base, ".") || !strcmp(base, "..") || strchr(base, '\\')) {
    return false;
  }
  struct stat info;
  if (lstat(source, &info) < 0 || !S_ISREG(info.st_mode)) {
    return false;
  }

  memcpy(path, source, strlen(source) + 1);
  memcpy(name, base, name_length + 1);
  return true;
}

bool paste_shell_quote(const char *value, char *quoted, size_t capacity)
{
  size_t needed = 3;
  if (capacity < needed) {
    return false;
  }
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    if (!printable(*p)) {
      return false;
    }
    size_t extra = *p == '\\' || *p == '"' ? 2 : 1;
    if (extra > capacity - needed) {
      return false;
    }
    needed += extra;
  }

  char *write = quoted;
  *write++ = '"';
  for (const char *p = value; *p; ++p) {
    if (*p == '\\' || *p == '"') {
      *write++ = '\\';
    }
    *write++ = *p;
  }
  *write++ = '"';
  *write = '\0';
  return true;
}
