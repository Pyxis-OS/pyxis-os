# TCC in Pyxis

The normal image includes a guest C compiler at `app://tcc.pxe` and a read-only
SDK at `app://sdk`. TCC compiles and links native applications inside Pyxis;
GCC remains the compiler for the OS and maintained userland. See the
[edit/build/run walkthrough](../development/edit-build-run.md) for using it with Kilo.

## Compilation and linking

From the shell's initial `home://` directory:

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
libterm, libpyxis, libtcc1 and libgcc until no more archive members are extracted.
Explicit archives and `-l` inputs retain command-line order; put them after their
users. `-nostdlib` omits startup and default libraries. The TCC support archive
supplies its varargs and selected arithmetic helpers; target libgcc supplies
the remaining compiler helpers. Their ownership is recorded with the recipe.

The linker retains TCC's symbol resolution and relocation machinery. Native
output uses `_start`, a fixed base of `0x400000`, page-aligned permission changes
and no writable/executable segments. BSS tails occupy memory without file bytes.
The writer checks layout bounds against both P1F and the upstream linker's
signed-int offsets. The [P1F contract](../../include/pxe/p1f.h) and kernel loader
are unchanged. ELF objects can contain debug information; P1F executables do not.

## Resources and paths

The shell supplies console output, private-memory management, readable `app://`
and writable `home://` grants, and an inherited working directory. TCC needs no
launcher authority. Reading source from stdin additionally requires console
input. Alternative launchers must provide the corresponding grants; paths do
not confer access by themselves.

An explicitly configured native USB mount can also hold source, objects and
executable output when the disk qualifies for writes and the session receives
the writable root grant. The compiler and SDK remain in `app://`; see the
[persistent USB development walkthrough](../development/edit-build-run.md#persistent-usb-development)
for mount configuration, synchronization and checking files after a restart.

Relative paths use the inherited directory chain; a leading `scheme://` selects
a named root. Quoted includes search beside their source first. Rooted includes
are opened directly, without falling back to search directories. Each `-I` or
`-L` argument is one path, preserving URI colons; repeat the option for multiple
directories. Overlong filenames are diagnosed rather than truncated.

The [guest SDK layout](../development/ports.md#tcc-and-the-guest-sdk) contains shared headers,
startup and runtime archives, TCC's four private headers and support archive,
and source/license provenance. It excludes host compilers, host elf2pxe and
GCC's private headers. `-print-search-dirs` displays the configured paths.

## Remaining limits

- No self-hosting or complete language/ABI coverage is claimed. TCC is built by
  Pyxis GCC; it need not compile the kernel or all runtime implementation sources.
- Shared libraries, PIE, TLS, indirect functions, dynamic relocations,
  constructor/destructor arrays, linker scripts and arbitrary `-Wl` controls
  are unsupported. Strong unresolved symbols fail the link.
- JIT/`-run`, archive creation, compiler subprocess dispatch, dependency generation,
  bounds checking, coverage and runtime backtraces are not provided.
- Native `#pragma once` is rejected until the filesystem provides
  [file identity](../technical-debt.md#file-identity-across-capability-paths).
  Use include guards.
- `__DATE__` and `__TIME__` use [UTC wall time](../kernel/wall-clock.md), preserving C's
  macro spelling. Missing time or dates outside years 0000–9999 are diagnosed.
  `-bench` uses monotonic elapsed time and upstream floating-point output. Both
  need a readable startup clock; the shell supplies it. Timezone selection is
  deferred. The unsigned millisecond benchmark interval must be under 49 days.
- Output uses create/truncate streams. A failed write can leave a partial file;
  compilation does not publish output by atomic replacement. `home://` contents
  are lost on reboot. An explicitly configured, qualified writable USB mount
  can keep source and output across boots; follow the
  [USB walkthrough](../development/edit-build-run.md#persistent-usb-development)
  to synchronize and verify them. An optional writable `host://` export is
  another persistence path; see its
  [walkthrough](../devices/virtio-fs.md#persistent-development-walkthrough).
- Each process has a fixed 1 MiB stack without growth and an unmapped guard
  page below it. Recursive parsing and larger inputs can exceed it. The largest fixed compiler frame observed in the
  GCC build was 2,720 bytes, not a bound on total stack use or source complexity.
  Heap backing grows through private-memory requests and is reclaimed at exit;
  there is no compiler-specific resource quota.

Ordinary guest builds have exercised cat, shell preprocessing, Mandelbrot,
TCC-generated variadic code with GCC-built runtime helpers, and compile/link
error reporting. These establish a useful application workflow, not exhaustive
compiler conformance or a promise that arbitrary inputs fit available resources.

## Source and maintenance

The ports recipe pins TinyCC `3dc99dbc82f8e07308c5d398136803e62f9676df`
(`0.9.28rc`) and records six ordered patches: target defaults, helper ownership,
FP scratch storage, native streams/paths, guest driver and P1F output.
[Port instructions](../../ports/tcc/README.md) describe rebuilding, host-running
TCC, patches and licensing; the SDK carries the pin and patch copies.

Adapt the compiler to Pyxis's capability, URI, process and executable contracts.
Reusable C facilities belong in libc; compiler policy and Unix assumptions belong
in the port. Unsupported features should fail explicitly, without a POSIX kernel
facade or successful-looking stubs. Compiler and runtime source notices differ;
retain both rather than treating them as one blanket license.
