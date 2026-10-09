# Wc, tail and sort

The normal image includes upstream sbase `wc`, `tail` and `sort` at `bin://wc.pxe`,
`bin://tail.pxe` and `bin://sort.pxe`; the shell resolves the bare commands. They
are built from sbase `c546c3a5724c81cee9a11d816a38ccdf17472129` by the
[sbase recipe](../../ports/sbase/README.md), alongside cksum, tee, uniq and
sha256sum. `tail` is built without its follow mode. The sbase MIT license, the
arg.h notice and the notices for the OpenBSD `strtonum.c` and `reallocarray.c`,
Pascal Gloor's `memmem.c` and the BSD `queue.h` are installed under
`boot://share/licenses/sbase`.

```text
wc host://notes.txt
cat host://notes.txt | wc -l
tail -n 20 host://log.txt
tail -n +100 host://data | head -n 3
sort -n host://numbers.txt
cat host://words | sort | uniq -c | sort -nr | tail -n 5
sort -o home://sorted.txt host://words
```

## Wc

Wc prints, for each operand, the line count, word count and then the byte count
(`-c`) or character count (`-m`), separated by single spaces and followed by the
operand name. `-l`, `-w`, `-c` and `-m` select counts; with none, it prints lines,
words and bytes. Several operands add a `total` line. With no operand it reads
stdin and prints no name; a `-` operand reads stdin and is labelled `<stdin>`.

Characters are decoded UTF-8. Bytes that do not form valid UTF-8 are not counted
by `-m` and are not spaces. Words are separated by the Unicode space table
from libutf, not by locale. A missing operand is reported, later operands still
count and the status is 1. Invalid options print the usage line, status 1.

## Tail

Tail prints the last 10 lines. `-n N`, `-c N` and `-m N` select the last N lines,
bytes or characters, and `-N` means `-n N`. A `+` before the number counts from the
start instead: `-n +3` prints from the third line, and `-c +5` from the fifth
byte. A negative count is its magnitude, so `-n -2` is `-n 2`, and `-n 0` prints
nothing. A final line without a newline is printed without one. Several operands
are each preceded by a `==> name <==` header; `-` reads stdin.

Tail does not follow files. `-f` and `-F` print `tail: -f is not supported: files
cannot be followed` (with the letter given) and exit with status 1 before any
output, even alongside other options and operands. Pyxis has no operation that
waits for a file to grow, and a polling loop would not follow honestly. See
[technical debt](../technical-debt.md#sbase-tail-follow-and-sort-limits).

Tail reads with `read` and keeps only the tail in a growing buffer, so memory
follows the retained text, not the input size. Headers go through stdio, which is
unbuffered on Pyxis, so they stay in order with the file bytes written
directly, including when stdout is redirected.

## Sort

Sort reads all operands (or stdin) into memory, orders the lines and writes them.
The options are upstream's:

| Option | Effect |
| --- | --- |
| `-r` | Reverse the order |
| `-u` | Write only the first of lines that compare equal under the selected keys |
| `-n` | Compare keys as numbers with `strtod`; text that is not a number counts as 0 |
| `-f` | Fold lowercase to uppercase using libutf's rune tables |
| `-d`, `-i` | Dictionary order (blanks and alphanumerics); ignore non-printing characters |
| `-b` | Skip leading blanks in key fields |
| `-k START[,END]` | Key from field and character positions, each optionally with `bdfinr` |
| `-t DELIM` | Field delimiter string, with backslash escapes; empty is an error |
| `-c`, `-C` | Check whether input is sorted; `-c` reports the first disordered line, `-C` is silent |
| `-m` | Accepted; the files are sorted together, not streamed |
| `-o FILE` | Write to FILE, opened after all input is read, so it may be an input |

Comparison is bytewise: there is no locale collation. Upstream appends a
whole-line key to the selected ones, so lines that differ always have one fixed
order, except under `-u`. Options upstream does not define, such as `-g`, `-h`,
`-M`, `-s`, `-V` and `-z`, print the usage line and exit with status 2, as do an
unreadable operand, an invalid key and a missing option argument. A failed check
and an empty delimiter exit with status 1.

## Port surface

Wc and sort sources are unmodified. Recipe patch 0004 restores the declarations
in upstream's util.h that the narrowed header had removed. Patch 0005 changes
tail only: it deletes the follow loop, the `fstat` call that chose whether to
follow and the `sleep`, drops `-f` from the usage line and makes `-f` and `-F`
refuse. Libc needed one addition, ISO C `bsearch`, which libutf's `isspacerune`
calls; its contract is in the [userspace libc notes](../kernel/userspace.md). The
`memmem` and `reallocarray` that sort needs come from sbase's own libutil, and no
kernel interface or compiler change was needed.

## Limits

- Sort uses libc `qsort`, an unstable heapsort. Lines that differ in any byte keep
  a fixed order because of the whole-line key, except under `-u`, where
  `sort -fu` on `Banana` and `banana` kept `Banana` while a host using a
  different `qsort` kept `banana`. Do not rely on which equal line `-u` keeps.
- Sort holds the whole input in memory and `-m` does not merge in a stream.
- `sort -o FILE` does not check the write to FILE or its close, as upstream; a
  failure there is not reported.
- Wc, tail and sort read stdin to EOF. The framebuffer console has no EOF
  operation, so use a file, redirect or pipeline; remote sessions deliver EOF.
  See [console input completion](../technical-debt.md#console-input-completion).
  Console stdin is also one native read per byte; see
  [console line input](../technical-debt.md#console-line-input).
- There is no locale support. Word splitting, character classes and case folding
  use libutf's tables, which upstream generates from the Unicode character
  database and distributes without a separate notice.
- The compiler reports sign-comparison warnings in tail.c and sort.c and a
  constant-conversion warning in libutil/unescape.c. All are upstream's and are
  retained.
- No timing was measured. Throughput of these filters is unqualified.

## Validation evidence

The image was built from Pyxis main `2b67e61a` plus this change (userland
`8744371`, ports `de9966c`) with the pinned Clang 23.1.3 toolchain. It ran in QEMU
10.2.2 on q35 with KVM, four CPUs, 2 GiB, virtio-fs (virtiofsd 1.14.0) and
virtio-net on the development host. The Remote session's host client in machine
mode ran 159 commands with the working directory on `host://`, redirecting stdout
into `host://`. Upstream sbase from the same commit, unpatched and built with host
GCC, ran the same commands under `LC_ALL=C`, with `stdbuf -o0` so that tail's
mixed stdio and `write` output kept its order. The host's own `tail` and `wc` were
not used.

Inputs were an empty file, a file of one newline, files with and without a final
newline, blank lines, 12,000-byte lines, UTF-8 with non-breaking and wide spaces,
invalid UTF-8, embedded NULs, tabs, a comma-separated table, numbers with signs,
exponents, hex and leading blanks, mixed-case words and a 5,000-line file.

- 22 `wc` cases: each input, every option combination, several operands, stdin
  by redirect and by `-`, a missing operand and an invalid option.
- 63 `tail` cases: the default on eight inputs; `-n`, `-c`, `-m` and `-N` with
  counts of 0, 1, more than the input, `+N` forms and a negative count, on three
  inputs; several operands, stdin and `-`, the large file, a missing operand and
  an invalid count.
- 69 `sort` cases: the default order on twelve inputs; `-r -n -nr -u -ru -f -fu
  -d -i -b -bn` on three; `-t`, `-k` with `n`, `r`, `b` and character offsets;
  `-n`, `-r`, `-u` and `-k` on the 5,000-line file;
  `-c`, `-C` on sorted and disordered input; `-m`, several operands, stdin and `-`;
  and a missing operand, an unknown option, an invalid key and an empty delimiter.
- 5 pipelines such as `cat dupes.txt | sort | uniq | wc -l`. The guest used its
  native `cat` and `head` and sbase `uniq`; the host used GNU `cat` and `head`.

The guest output matched the host byte for byte in 158 of 159 cases. The exception
is `sort -fu` on mixed-case words, described under Limits. Exit statuses matched
in 158 of 159; `sort big.txt | head -n 3` exited 141 on the host (SIGPIPE) and 0
in the guest, with identical output. Diagnostics matched upstream apart from the
program path prefix and native error text, such as `Not found` for a missing file.
Before the host references used `stdbuf`, three `tail` cases differed only by the
order of headers, because upstream mixes buffered `printf` and unbuffered `write`;
the guest's unbuffered stdio gives the in-order result.

Guest-only checks:

- `tail -f`, `-F`, `-f` with two operands and `-n 2 -f` each printed the refusal
  and exited 1 without output.
- Scheme paths: `wc tmp://w.txt`, `sort -r tmp://w.txt`, `tail -n 3 tmp://sorted.txt`,
  `tail -n 1 host://in/ten.txt` and `wc -l home://s.txt` worked. `wc system://nothing`
  failed with `Not found` and status 1, since `system://` is not bound on a live
  boot.
- `sort -o` wrote to `tmp://` and `home://`, and `sort -o tmp://inplace.txt
  tmp://inplace.txt` replaced its own input with the sorted lines. `sort -o
  boot://s.txt` failed with `Permission denied`, status 1, and created nothing.
- On a pty, the interactive remote client typed `cat big.txt | sort | uniq | wc -l`
  and printed 4976, as the host does, and `sort -n pairs.csv | tail -n 2`,
  `wc -l words.txt nums.txt` and `tail -f ten.txt` behaved as above.

Not done: the local Development tab was not driven with a keyboard, and no
native ThinkPad run was made.
