# TCC in Pyxis

The normal image includes a guest C compiler, run as `tcc` (`bin://tcc.pxe`), and
a read-only SDK at `boot://sdk`. TCC compiles and links native applications inside Pyxis;
Clang on the host remains the compiler for the OS and maintained userland. See the
[edit/build/run walkthrough](../development/edit-build-run.md) for using it with Kilo.

## Compilation and linking

From the shell's initial `home://` directory, or any writable directory:

```text
tcc hello.c -o hello.pxe
./hello.pxe
tcc -c hello.c -o hello.o
tcc hello.o -o hello.pxe
tcc -E hello.c
```

Executable output is static P1F, defaulting to `a.pxe` when `-o` is omitted.
No guest converter or host build is involved. `-c` produces x86-64 ELF objects;
static libraries remain ordinary `.a` archives. Multiple source/object/archive
inputs, response files, include paths and macro definitions are supported.
The [port reference](../../ports/tcc/README.md#guest-sdk-and-use) lists accepted
options; `tcc -h` and `tcc -hh` describe the driver and additional flags.

The target uses the existing LP64/System V x86-64 ABI, defines `__pyxis__`, and
supports SSE2 float/double and x87 long double. Public SDK headers are usable by
TCC, with its own compiler-dependent headers. This does not imply full GNU C23
support: the existing line editor's `[[fallthrough]]`, for example, is rejected.
Libc provides floating-point formatting alongside literal conversion and a
small math subset; a full libm remains deferred. See the [FP contract](../kernel/userspace.md#floating-point).

Default linking places `crt0.o` before application inputs, then rescans libc,
libterm, libpyxis, libtcc1 and the SDK's compiler runtime (compiler-rt
builtins) until no more archive members are
extracted.
Explicit archives and `-l` inputs retain command-line order; put them after their
users. `-nostdlib` omits startup and default libraries. The TCC support archive
supplies its varargs and selected arithmetic helpers; the compiler runtime
supplies the remaining compiler helpers. Their ownership is recorded with the recipe.

The linker retains TCC's symbol resolution and relocation machinery. Native
output uses `_start`, a fixed base of `0x400000`, page-aligned permission changes
and no writable/executable segments. BSS tails occupy memory without file bytes.
The writer checks layout bounds against both P1F and the upstream linker's
signed-int offsets. The [P1F contract](../../include/pxe/p1f.h) and kernel loader
are unchanged. ELF objects can contain debug information; P1F executables do not.

## Resources and paths

The shell supplies console output, private-memory management, its space's root
grants, such as readable `boot://` and writable `home://` and `tmp://`, and an
inherited working directory. TCC needs no
launcher authority. Reading source from stdin additionally requires console
input. Alternative launchers must provide the corresponding grants; paths do
not confer access by themselves.

An explicitly configured native USB mount can also hold source, objects and
executable output when the disk qualifies for writes and the session receives
the writable root grant. The compiler and SDK remain in `boot://`; see the
[persistent USB development walkthrough](../development/edit-build-run.md#persistent-usb-development)
for mount configuration, synchronization and checking files after a restart.

Relative paths use the inherited directory chain; a leading `scheme://` selects
a named root. Quoted includes search beside their source first. Rooted includes
are opened directly, without falling back to search directories
([includes by URI](#includes-by-uri)). Each `-I` or
`-L` argument is one path, preserving URI colons; repeat the option for multiple
directories. Overlong filenames are diagnosed rather than truncated.

The [guest SDK layout](../development/ports.md#tcc-and-the-guest-sdk) contains shared headers,
startup and runtime archives, TCC's four private headers and support archive,
and source/license provenance. It excludes host compilers and
Clang's private headers. `-print-search-dirs` displays the configured paths.

## Includes by URI

Accepted 2026-10-10 ([#693](https://git.internal/PyxisOS/pyxis-os/pulls/693)).
Any `scheme://NAME` is a rooted include, in quote and angle form: it is opened as
given, never prefixed with a search directory. `home://`, `boot://`, `host://`,
`tmp://` and `http(s)://` names work the same way in `#include`, `-include` and
as command-line sources. TCC opens `http(s)://` through the caller's namespace
exactly where `cat` can, as an ordinary [HTTP snapshot](http-fetch.md); it needs
no extra grant.

```c
#include "https://gist.githubusercontent.com/RabaDabaDoba/145049536f815903c79944599c6f952a/raw/fb1503af3caaf4d1188f3f9b6c356b219d6a06eb/ANSI-color-codes.h"
```

- **Relative includes in a fetched file** resolve against its final URL, after
  redirects, by [RFC 3986](https://www.rfc-editor.org/rfc/rfc3986.html#section-5.2):
  `"../b.h"` from `https://h/inc/sub/a.h` fetches `https://h/inc/b.h`. The base's
  query and fragment are dropped. C's order still applies: beside the file first,
  then the search directories, so a local `-I` directory can supply a header the
  server lacks. Native paths keep their unnormalized traversal.
- **Failures:** only *not found* (an HTTP 404 or 410, or an unknown host)
  continues the search. Any other failure of an `http(s)` candidate stops with
  `could not open 'URI': <cause>`, so a same-named local header never stands in
  for a remote one that failed. A failed open reports errno, not the HTTP status.
- **`#pragma once`:** fetched files have no object identity. Within one
  translation unit, two are the same header when their final URLs match after
  normalization (lower-case scheme and host, no default port, no dot segments,
  no fragment; the query is kept). A once-marked URI named again, in any
  equivalent spelling, is skipped without a request. A different name is fetched
  and skipped if it redirects to the same final URL. The first snapshot stays
  open until the translation unit ends.
- **Search directories must be local:** `-I`, `-isystem`, `-L` and `-B` refuse
  `http(s)://`, because a remote directory turns every header search into a
  request. Name remote headers in `#include`.
- **Pinning:** TCC keeps no cache, and every compile fetches again. A build is
  repeatable when its URIs are immutable:
  `raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>`, a gist's
  `raw/<revision>/<file>`, or a gist's per-file `raw/<blob>/<file>` (the file's
  git blob ID). A branch name such as `master` moves. Verifying an optional
  `#sha256=` fragment is possible later work.

The [QEMU record](../development/experiments/tcc-uri-includes/README.md)
compiles the ANSI gist and `stb_image.h` at a pinned commit.

## Remaining limits

- No self-hosting or complete language/ABI coverage is claimed. TCC is built by
  the Pyxis Clang; it need not compile the kernel or all runtime implementation sources.
- Shared libraries, PIE, TLS, indirect functions, dynamic relocations,
  constructor/destructor arrays, linker scripts and arbitrary `-Wl` controls
  are unsupported. Strong unresolved symbols fail the link.
- JIT/`-run`, archive creation, compiler subprocess dispatch, dependency generation,
  bounds checking, coverage and runtime backtraces are not provided.
- Native `#pragma once` compares [live file identity](../interfaces/file-metadata.md),
  including differently named aliases. One stream per once header remains open
  until translation-unit cleanup, including error cleanup. Replacing a pathname
  identifies a new header. Missing identity at the directive or on an include
  requiring comparison produces a diagnostic; handle/backing limits can fail
  compilation. Borrowed stdin remains caller-owned. Include guards still work.
  Fetched `http(s)` headers compare by [final URL](#includes-by-uri) instead.
- `__DATE__` and `__TIME__` use [UTC wall time](../kernel/wall-clock.md), preserving C's
  macro spelling. Missing time or dates outside years 0000–9999 are diagnosed.
  `-bench` uses monotonic elapsed time and upstream floating-point output. Both
  need a readable startup clock; the shell supplies it. Timezone selection is
  deferred. The unsigned millisecond benchmark interval must be under 49 days.
- Output uses create/truncate streams. A failed write can leave a partial file;
  compilation does not publish output by atomic replacement. Installed systems
  keep `home://` on the npfs pool across reboots; on live boots `home://` and
  `tmp://` are RAM-backed. A configured, qualified writable USB volume can also
  keep source and output; follow the
  [USB walkthrough](../development/edit-build-run.md#persistent-usb-development)
  to synchronize and verify them. An optional writable `host://` export is
  another persistence path; see its
  [walkthrough](../devices/virtio-fs.md#persistent-development-walkthrough).
- Each process has a fixed [1 MiB stack](../kernel/program-loading.md) without growth and an unmapped guard
  page below it. Recursive parsing and larger inputs can exceed it. The largest fixed compiler frame observed in the
  Clang 23 build was 2,824 bytes, not a bound on total stack use or source complexity.
  Heap backing grows through private-memory requests and is reclaimed at exit;
  there is no compiler-specific resource quota.

Ordinary guest builds have exercised cat, shell preprocessing, Mandelbrot,
TCC-generated variadic code with compiler-built runtime helpers, and compile/link
error reporting. These establish a useful application workflow, not exhaustive
compiler conformance or a promise that arbitrary inputs fit available resources.

## Source and maintenance

The ports recipe pins TinyCC `3dc99dbc82f8e07308c5d398136803e62f9676df`
(`0.9.28rc`) and records nine ordered patches: target defaults, helper ownership,
FP scratch storage, native streams/paths, guest driver, P1F output, guest
clocks, native once-header identity and URI includes.
[Port instructions](../../ports/tcc/README.md) describe rebuilding, host-running
TCC, patches and licensing; the SDK carries the pin and patch copies.

Adapt the compiler to Pyxis's capability, URI, process and executable contracts.
Reusable C facilities belong in libc; compiler policy and Unix assumptions belong
in the port. Unsupported features should fail explicitly, without a POSIX kernel
facade or successful-looking stubs. Compiler and runtime source notices differ;
retain both rather than treating them as one blanket license.
