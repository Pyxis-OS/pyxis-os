#ifndef REMOTE_PASTE_H
#define REMOTE_PASTE_H

#include <stdbool.h>
#include <stddef.h>

#define PASTE_PATH_CAPACITY 1025u
#define PASTE_NAME_CAPACITY 201u

/* Decode one printable ASCII shell word naming an absolute or ~/ regular file.
 * Relative paths, expansions and unquoted shell operators are ordinary paste.
 * The source is checked again by the transfer before it is read. */
bool paste_file_path(const unsigned char *data, size_t length,
    char path[PASTE_PATH_CAPACITY], char name[PASTE_NAME_CAPACITY]);

/* Produce one argument for the Pyxis shell's parse_line quoting rules. */
bool paste_shell_quote(const char *value, char *quoted, size_t capacity);

#endif
