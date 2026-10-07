# Everyday commands

Status: **milestone, agreed 2026-10-07.** The owner's request is small
improvements for day-to-day use, picked from the
[everyday gaps](everyday-gaps.md) list. Codex takes it after the remote debugging
milestone closes; each task starts when the owner says so.

## Goal

Make the shell less awkward for ordinary work: writing a line to a file,
copying a file, and listing a directory that doesn't fit on the screen.

These are native commands, following the
[ports and native commands](../development/ports.md#ports-and-native-commands)
boundary. They work with Pyxis roots and directory capabilities, so they are
not BusyBox applets.

## Today

- **`echo` is missing.** `echo hi > note.txt` reports `echo: Not found`; the
  workaround has been `cat boot://share/hello.txt > FILE`. A shell builtin
  wouldn't help, because builtins reject redirects and can't be pipeline stages
  ([shell](../userland/shell.md)).
- **`cp` is missing.** Copying a file needs `cat SRC > DST`.
- **`ls` is minimal** (`userspace/ls/main.c`, about 80 lines). It accepts only
  paths, so `ls -a` reports `-a: Not found`.
  - It prints one name per line, with `/` after directories, in the order the
    filesystem returns them.
  - A large directory doesn't fit on the screen. `boot://` alone has about 50
    entries, and the terminal has no scrollback.
  - `ls boot:// | less` already pages a long listing
    ([less](../userland/less.md)).
- **The pieces exist:**
  - `term_size` reports the terminal's columns;
  - directory enumeration reports each entry's kind;
  - files report their size.

  There are no modification times in the native filesystem, and none are planned.

## Decisions

Accepted by the owner on 2026-10-07:

1. **Scope:** `echo` and `cp` as small native programs, and a better `ls`.
   - Everything else stays in the gaps list. Text tools such as `grep`, `wc`
     and `tail` remain [application port](application-ports.md) candidates.
   - **Terminal scrollback is not in this milestone,** even though it is the
     general fix for output scrolling away. It changes how the TTY stores text,
     which the [display milestone](display-drivers.md)'s resize work (task 5a) is
     changing now. It is a candidate for right after that.
2. **`ls` behaviour.**
   - **Sorted by name**, by byte order.
   - **On a terminal:** names in columns sized to the terminal width, colored by
     kind:
     - directories;
     - regular files;
     - programs (`.pxe`, since Pyxis has no execute bit);
     - scripts starting with `#!`.
   - **In a pipe or file:** one name per line and no color, as today, so
     scripts and `less` keep working.
   - **Options:**
     - `-1` forces one per line;
     - `-l` gives a long listing: kind, size and name. There is no time column.
3. **`cp` behaviour.**
   - `cp SRC DST`, or several sources into a directory.
   - Files only at first; `-r` for directories comes later.
   - An existing destination file is replaced, as Unix `cp` does.
   - Copies work across roots, for example `cp boot://share/hello.txt home://`.
   - A failed copy leaves no partial destination behind where the filesystem
     makes that possible, and otherwise reports the partial file.

## Tasks

- [ ] **1. `echo`.** It prints its arguments separated by spaces, then a
  newline; `-n` drops the newline. No escape processing.
  - **Finish when:** `echo hi > home://note.txt`, `echo a b | cat` and
    `echo -n x` behave as expected.

- [ ] **2. `ls`.** Sorting, terminal columns, colors, `-1` and `-l`
  (decision 2), with usage errors for unknown options.
  - **Finish when:**
    - `ls boot://` fits in a few rows on a terminal, with colored kinds;
    - `ls boot:// | cat` prints one plain name per line;
    - `ls -l` shows kinds and sizes.

- [ ] **3. `cp`** (decision 3).
  - **Finish when:**
    - copying within a root and across roots gives identical files, checked
      with `sha256sum`;
    - copying several files into a directory works;
    - a read-only destination fails cleanly.

- [ ] **4. Tidy the gaps list.**
  - Remove the entries this milestone closes.
  - Check whether "running shell scripts as commands" still fails in a space
    with `launch = true`, since the Lua runtime milestone gave foreground
    commands a launcher there. Remove it or update it.

## Working rules

- Small native programs in `userspace/`, with their usage in the shell or
  command docs.
- No permanent diagnostic output.
- Ordinary QEMU checks are enough. The owner's ThinkPad use is the real test.

## Related

[Everyday gaps](everyday-gaps.md), [shell](../userland/shell.md),
[terminal](../userland/terminal.md) and
[application ports](application-ports.md).
