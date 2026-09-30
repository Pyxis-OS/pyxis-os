# A small native port: sbase uniq

Status: tasks 1 and 2 complete; guest validation (task 3) remains. This is a
bounded consumer milestone for trying a new development harness, not an
assignment to build or change the harness itself. It is independent of the
native filesystem mount milestone.

## Completion point

Package upstream sbase `uniq` in the normal image, runnable from the local and
remote shell. It should remove adjacent duplicate lines, support the agreed
upstream options, read files or stdin, and write to a file or stdout through the
existing libc. It does not sort its input or remove non-adjacent duplicates.

This exercises argument handling, dynamically sized lines, allocation, stdio,
pipelines, redirection and error reporting without new kernel facilities.
The result must be an upstream port, not a Pyxis rewrite of the utility.

## Source and scope

Extend the existing `sbase` recipe used by `cksum` and `tee`. Its current upstream
pin is `c546c3a5724c81cee9a11d816a38ccdf17472129`; retain that revision and its
license rather than updating unrelated utilities. Inspect the pinned `uniq.c`
and its actual helper dependencies before deciding which changes are necessary.
The [upstream source browser](https://git.suckless.org/sbase/file/uniq.c.html) is
useful context, but the recipe's pinned source is authoritative.

The target is ordinary deduplication, `-c`, `-d`, `-u`, field/character skipping
with `-f`/`-s`, and upstream input/output operands, including `-` where supported.
Task 1 must verify those behaviors and their dependencies against the pin. If a
feature would materially expand the work, discuss it before implementing or
silently removing it.

Keep file access and line processing in upstream code. Missing conventional
libc operations belong in libc with their normal contracts; do not substitute
Pyxis-specific file-reading code inside `uniq`. `getline` is one dependency to
check, not an assumed missing function. Inventory transitive helpers as well as
direct calls; compiling a helper must not pull in the entire sbase suite.

Preserve upstream formatting, assertions, allocation behavior, diagnostics and
exit-status policy. Each source patch needs a concrete compatibility reason.
Do not add general hardening, a formatter replacement, a new option parser,
locale infrastructure, kernel APIs or optional upstream machinery unrelated to
this utility. An unexpected large dependency is a discussion point, not implicit
authorization. Existing `cksum` and `tee` must retain their behavior.

## Probe results and agreed scope

Task 1 compiled and linked the pinned `uniq.c` against the SDK exported by
`make sdk` from Pyxis `945e8d3` (userspace `c9ed311`, ports `6ff8504`), using
the installed GCC 16.2.0/binutils 2.47 and the flags in `share/pyxis.mk`.
The source was sbase `c546c3a5724c81cee9a11d816a38ccdf17472129` with the
recipe's patches 0001 and 0002 applied. This was a compile/link probe only; no
guest run was attempted.

The helper closure is `libutil/strtonum.c`, `ealloc.c`, `eprintf.c` and
`fshut.c`. Only `strtonum.c` is new to the recipe; it uses libc `strtoll`,
`errno` and `LLONG_MIN`/`LLONG_MAX`. From `text.h`, uniq uses only
`struct line`; `getlines` and `linecmp` are not referenced. The pinned option
set is exactly `-c`, `-d`, `-u`, `-f`, `-s` and `[input [output]]`, with `-`
selecting stdin or stdout, so the target scope needs neither reduction nor
expansion.

With the recipe header as patched, compilation reported `isblank`, `getline`,
`estrtonum` and `INT_MAX` as undeclared. Restoring the upstream `compat.h`
include (which supplies `<limits.h>`) and `strtonum` declarations left
exactly two undefined symbols at link time: `getline` and `isblank`.

Agreed changes:

- **Recipe patch 0003** restores the upstream `compat.h` include and `strtonum`
  declaration block in the narrowed private `util.h`, unchanged from upstream.
  The Makefile adds uniq's object list, and `uniq.pxe` is staged with cksum/tee.
  `libutil/strtonum.c` carries an OpenBSD ISC notice not covered by sbase's
  LICENSE, so task 2 stages that file beside `arg.h` and records the recipe
  license as MIT AND ISC.
- **Libc `isblank`** returns true for ASCII space and tab. As with the other
  classification functions, EOF, non-ASCII bytes and other negative arguments
  return zero; the negative-argument result is a Pyxis extension, not a
  standard C guarantee.
- **Libc `getline`** only; `getdelim` has no consumer and is not added. It uses
  the POSIX signature, and `stdio.h` includes `sys/types.h` for `ssize_t`.
  - The returned count includes the newline and excludes the terminating NUL.
    Embedded NUL bytes are ordinary input.
  - A final line without a newline returns successfully. EOF before any byte
    returns -1.
  - Null `lineptr` or `n` fails with EINVAL. The buffer grows with `realloc`,
    with checked growth arithmetic and an `SSIZE_MAX` result limit (EOVERFLOW).
  - Allocation and overflow failures set errno and the stream error indicator,
    as musl does, so upstream `fshut` reports the failure instead of exiting
    successfully with truncated output.
  - After any failure, `*lineptr` and `*n` describe the caller's current
    allocation, which may be a buffer from earlier successful growth.
  - It loops over `fgetc`. Streams are unbuffered and have no pushback, so
    each byte is a separate native read and possibly a filesystem worker
    round trip, which is especially costly for `host://` and native
    filesystem inputs. Stdio buffering remains separate libc work; uniq is not
    rewritten to compensate.

Retained upstream behavior: `-d` with `-u` prints nothing; without SIGPIPE, a
closed output sets the stream error, which `fshut` reports after input
reaches EOF; console input has no EOF, as for cksum and tee. GCC's `loff`
may-be-uninitialized
warning is a false positive by inspection: `loff` is assigned and read only
under the same non-null `l`, which is not modified between those points. It is
retained, like cksum's signedness warning.

## Focused tasks

1. [x] **Probe the pinned consumer and settle the compatibility scope.** Compile
   and link `uniq` and the necessary upstream helpers against the current SDK.
   Record the exact source/dependency revisions, commands and actual gaps.
   Identify whether existing libc covers line input, character classification,
   allocation, numeric parsing and stream cleanup. Propose the smallest required
   changes and discuss unresolved semantics or meaningful scope expansion before
   implementing. Do not treat a compile-only probe as a working guest port.
2. [x] **Implement the compatibility surface and package the port.** Add only
   agreed reusable libc prerequisites, if any, in userland. Extend the existing
   ports recipe and install selection with `uniq.pxe`, preserving licenses and
   upstream behavior. Publish dependency PRs before updating Pyxis pins; document
   merge order. Make focused commits and avoid unrelated pin changes. No kernel
   or compiler-container change is expected; report an actual need before
   expanding scope.
3. [ ] **Validate the consumer and close the milestone.** Build the ordinary
   image and exercise the packaged command through the existing remote terminal,
   with a local-shell smoke check. Compare output and exit status with the same
   upstream revision built on the host. Record actual coverage and remaining
   limitations, check CI for exact submitted revisions, then replace this WIP
   with a concise userland reference and update its links.

## Acceptance and validation

Use small known inputs so differences are easy to inspect. Cover:

- Adjacent repeats, non-adjacent repeats and the count/duplicate/unique options.
- Field and character skipping under the agreed upstream behavior.
- Empty input, blank lines, long lines spanning multiple native reads and a final
  line without a newline. Use identical bytes and the host C locale for comparison.
- Named input/output, stdin/stdout, shell pipelines and redirection; show that a
  producer's closure lets the pipeline finish normally.
- Missing input and denied output authority, checking diagnostics and failure
  status without requiring identical host and Pyxis error wording.
- Existing `cksum` and `tee` after changes to shared recipe helpers or libc.

Use ordinary builds, interactive QEMU and the existing remote client. Screenshots
are unnecessary for these text results. No new tests, test infrastructure, fault
injection, CI workflows or boot/output automation are included. Record the QEMU
CPU count and accelerator, clean up owned processes, and distinguish checks run
from source inspection. Documentation-only task PRs do not require a boot.

Follow the [port workflow](../development/ports.md),
[repository ownership](../development/sdk-and-repositories.md),
[libc portability contract](../userland/libc-portability.md) and
[remote terminal guide](../userland/remote-terminal.md).
Other utilities, performance work and harness development remain separate tasks.
