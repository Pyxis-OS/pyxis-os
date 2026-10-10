# Grep

The normal image includes upstream sbase `grep` at `bin://grep.pxe`; the shell
resolves the bare command. It is built from sbase
`c546c3a5724c81cee9a11d816a38ccdf17472129` by the
[sbase recipe](../../ports/sbase/README.md), with recursive search added by
patch 0006. The sbase MIT license and notices are installed under
`boot://share/licenses/sbase`.

```text
grep -n main host://notes.c
grep -Ei 'error|warn' host://log.txt home://other.txt
grep -rl TODO home://src
cat host://words | grep -v '^#' | grep -c .
```

Sbase was chosen over BusyBox because its tools already build over libc one file
at a time, as `wc`, `tail` and `sort` do. BusyBox here is built from a
hand-written replacement for libbb that covers only vi, less and tar, so its grep
would first need the option, memory and directory helpers written.

## Behavior

Grep prints the lines of each operand, or of stdin, that match any pattern. A
pattern is a basic regular expression unless `-E` or `-F` is given; expressions
use libc's [regex engine](libc-portability.md#regular-expressions-and-utf-8-conversion)
with ASCII-only classes and case folding.

| Option | Effect |
| --- | --- |
| `-E`, `-F` | Extended expressions, or fixed strings |
| `-i` | Ignore ASCII case |
| `-v` | Select lines that do not match |
| `-n`, `-c` | Prefix line numbers, or print only a count |
| `-l`, `-q` | Print only the names of matching files, or nothing |
| `-r` | Search directories recursively |
| `-H`, `-h` | Always, or never, prefix the file name |
| `-w`, `-x` | Match whole words, or whole lines |
| `-e PATTERN`, `-f FILE` | Add patterns; each line of the text or file is one pattern |
| `-s` | Do not report unreadable files |

The name prefix `name:` appears when there are several operands, with `-H`, or
when `-r` searched a directory, and `-h` removes it. The prefix applies to `-c`
counts too (`name:3`), unlike upstream, which printed bare counts. Stdin is named
`<stdin>`, as upstream does. Output order follows the operands.

The exit status is 0 when a line was selected, 1 when none was and 2 for an error,
which wins over a match. Upstream has no `-L`, `-o`, `-m`, `-a`, context or
`--include` options; they print the usage line with status 2.

## Recursive search

`-r` treats a directory operand by reading it through libc `opendir` and
`readdir`, which are the native directory listing, so only entries the caller
can enumerate are searched. Names are joined to the operand with one `/`
(`home://src/main.c`), sorted bytewise, and each directory is searched where it
sorts. Entries the listing marks as symbolic links are skipped. With no operand
`-r` searches the working directory and prints names without a prefix. A single
file operand under `-r` prints no prefix. Without `-r` a directory operand is
an error.

A directory's entries are read and the handle closed before descending, so the
open handles do not grow with depth. Nesting beyond 64 levels is reported and
skipped. An unreadable directory or file is reported and the walk continues.

## Output

Stdio output is unbuffered, so upstream's `printf` and `puts` made two to four
native writes per line. Grep collects its output in a block-sized buffer and
writes it at the end of each file, or per line when reading stdin so that pipes
stay interactive. Diagnostics are written between files, so their order
relative to output is unchanged.

## Limits

- A line is matched only as far as the regex engine reads valid UTF-8 and up to
  its first NUL, so text after an invalid byte or a NUL is not seen; `-F` uses
  `strstr` and is unaffected. See
  [regex limits](../technical-debt.md#regex-character-classes-and-back-references).
  Grep prints the matching line anyway and never reports "binary file matches".
- Case folding and classes are ASCII-only; there is no locale.
- A pattern error prints `invalid regex:` with libc's text, not GNU's.
- Console stdin cannot reach EOF on the framebuffer console, so `grep` there
  does not finish; use a file, redirect or pipeline. Remote sessions deliver EOF.

## Validation

The image was built from Pyxis main `d01fe599` plus this change (ports `cf348aa`,
userland `fbce73bf`) with the pinned Clang 23.1.3 toolchain. It ran in QEMU 10.2.2
on q35 with KVM, four CPUs, 4 GiB, virtio-fs (virtiofsd 1.14.0) and virtio-net
on the development host. The Remote session's host client in machine mode ran
135 commands with the working directory on `host://`, and recorded each
command's output and exit status. The references ran the same arguments under
`LC_ALL=C` with `stdbuf -o0` in the same directory:

- 89 cases against upstream sbase `grep` from the same commit, unpatched and
  built with host GCC: every option above on a 9-line file, a file without a final
  newline, an empty file, a file of one newline, 12,000-byte lines, UTF-8 text,
  CRLF text, a 5,000-line file, `-e` and `-f` pattern lists including an empty
  pattern, invalid expressions, a missing file with and without `-s`, `-q`
  alongside an unreadable operand, unknown options and ten stdin forms.
- 46 cases against GNU grep 3.12, which upstream sbase cannot answer: several
  operands with `-n -c -l -h -H -q`, a missing operand among others, and `-r`
  over a directory tree with `-n -c -l -v -i -F -E -h -H -x -w`, a trailing
  slash, a file operand, an empty directory, two operands, a missing operand,
  names with spaces, a hidden file, a four-level directory and no operand. Recursive
  output was compared as sorted lines, since GNU grep uses directory order.

Output and status matched in 132 of 135 cases, with diagnostics matching apart
from the program path and native error text. The three exceptions are expected:
`-c` with stdin and a file prints `<stdin>:2` where upstream printed a bare `2`,
and GNU's `binary file matches` notice replaces the lines of the file with
invalid UTF-8 in two whole-tree searches. Regex error text comes from libc.

Beyond the comparison, `-r` was run on `boot://share` (682 files in 1.2 s),
`boot://share/licenses` and `host://` paths with and without a trailing slash;
a directory operand without `-r` failed with status 2; and a 70-level tree
searched down to level 64, reported the deeper directory and exited 2. Before
block output, `grep -n needle` printing 2,090 lines took 9.4 s on the remote
console and 2.3 s into a `host://` file; with it, 0.11 s to the console.
