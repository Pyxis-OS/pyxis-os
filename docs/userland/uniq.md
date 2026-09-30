# Uniq

The normal image includes upstream sbase `uniq` at `app://uniq.pxe`; the shell
resolves the bare command `uniq`. It is built from sbase
`c546c3a5724c81cee9a11d816a38ccdf17472129` by the
[sbase recipe](../../ports/sbase/README.md), which also packages cksum and tee.
The sbase MIT license, arg.h notice and OpenBSD ISC notice for
`libutil/strtonum.c` are installed under `app://share/licenses/sbase`.

```text
uniq host://input
cat host://input | uniq -c
uniq -d -f 1 host://input home://duplicates
uniq -u - home://unique < host://input
```

## Behavior

Uniq writes each run of adjacent identical lines once. It does not sort its input
or detect non-adjacent repeats. The upstream options are supported:

| Option | Effect |
| --- | --- |
| `-c` | Prefix each output line with its count, formatted as `%7ld ` |
| `-d` | Print only lines that were repeated |
| `-u` | Print only lines that were not repeated |
| `-f N` | Skip N fields before comparing; fields are runs of non-blanks after blanks |
| `-s N` | Skip N further characters before comparing |

Both `-d` and `-u` together print nothing, as upstream. Operands are
`[input [output]]`; `-` selects stdin or stdout. Relative names use the inherited
working directory. A named output is opened with `fopen(..., "w")`, creating or
truncating it through the caller's grants after the input opens.

Blanks are ASCII space and tab only, through libc `isblank`; non-ASCII bytes are
never blank. Lines are compared as bytes, including embedded NULs. A final line
without a newline is not equal to the same text with one, and is written without
a newline.

Invalid options, more than two operands and invalid or negative counts print
upstream usage or `strtonum` diagnostics and exit with status 1. A missing input or
unopenable output exits with status 1 before any output is written. Read and
write errors are reported by upstream `fshut` with status 1 after input reaches
EOF. There is no SIGPIPE: when a downstream reader closes, uniq continues reading
to EOF, then reports `ferror <stdout>`.

## Port surface

Uniq's source and helper bodies are unmodified. Recipe patch 0003 restores
upstream util.h's `compat.h` include and strtonum declarations, which the
narrowed cksum header had removed. Libc provides `isblank` and `getline`; their
contracts are in `ctype.h`, `stdio.h` and [libc I/O](stdio.md). No kernel
interface or compiler change was needed.

## Limits

- Line input is `getline` over unbuffered streams: one native read per byte.
  A 36,009-byte `host://` input took 20.4 s in the validation VM below. Treat
  large inputs as slow until stdio gains input buffering; see
  [unbuffered line input](../technical-debt.md#unbuffered-line-input).
- Console input has no EOF, so interactive stdin cannot finish; use a file,
  redirect or pipeline. See [console input completion](../technical-debt.md#console-input-completion).
- There is no locale support; comparison and field splitting use the C locale.
- GCC reports a may-be-uninitialized warning for `loff` in upstream code. It is a
  false positive by inspection and is retained.

## Validation evidence

The image built from `6c6ef84` (userland `8a12e9a`, ports `e70862a`) with
GCC 16.2.0/binutils 2.47 ran in QEMU 10.2.2 on q35 with KVM, four CPUs, 256 MiB,
virtio-fs (virtiofsd 1.14.0) and virtio-net, inside the development VM. Commands
ran through the Remote session's host client in machine mode, reading inputs
and writing outputs through `host://`. The same upstream revision, unpatched
and built with host GCC, ran on identical bytes under `LC_ALL=C`.

All 28 cases produced the same exit status, and all 23 output files were
byte-identical:

- Plain, `-c`, `-d`, `-u`, `-c -d` and `-d -u` on adjacent and non-adjacent repeats.
- `-f 1`, `-f 2`, `-s 2`, `-f 1 -s 1` and `-f 9` over space- and tab-separated fields.
- Empty input, blank lines, final lines without a newline, embedded NULs,
  non-ASCII bytes before a field, and 12,000-byte lines.
- Named input/output, `-` for stdin and for stdout, `<`/`>` redirection,
  `cat | uniq` and `uniq | cksum`.
- A missing input, `-x`, `-f abc`, `-s -1` and three operands. Diagnostics match
  upstream except for native error text, such as `Not found` for a missing file.

Guest-only checks: an output under read-only `app://` failed with
`Permission denied`, status 1, and was not created. In `uniq | head -n 1`,
head printed the first line, uniq reported `ferror <stdout>: Endpoint closed`
with status 1, and the pipeline completed. Output written to `home://` matched
by cksum. Guest cksum and tee still matched host GNU cksum and the input bytes.
A local-shell smoke check on the Development tab printed the expected `-c` result.
