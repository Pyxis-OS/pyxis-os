# TCC porting plan

Status: working plan for the first guest C compiler, following the completed
FP/Mandelbrot work. TCC has not been ported to Pyxis. This document records the
source audit, proposed boundaries and individual PR tasks for the
[edit/build/run milestone](edit-build-run.md). The [candidate comparison](guest-compiler.md)
remains background if the port grows beyond these boundaries. Discuss unresolved
choices before starting the affected task; this list is not blanket approval to
implement all tasks.

## Porting boundary

Adapt TCC to Pyxis, not the kernel or OS design to TCC. Keep the capability ABI,
URI namespaces, process model, filesystem semantics and P1F loader contract.
Missing standard C facilities can belong in libc when they are useful beyond
this compiler. Unix assumptions and compiler-specific policy belong in the
ports repository's TCC patches.

Small public-header changes that preserve semantics are welcome. For example,
replace `[[noreturn]]` with `__attribute__((noreturn))` where needed so both GCC
and TCC can consume a declaration. This does not require a compatibility macro
framework, changing the calling convention or downgrading first-party
implementations from GNU C23. Do not remove layout checks, diagnostic attributes
or useful guarantees merely to make a header parse; first check what TCC accepts.

Do not add POSIX syscalls, a public file-descriptor layer, global path authority,
signals, dynamic linking or process replacement solely to satisfy TCC. Small
port-local helpers around native streams/handles are fine; a general Unix
emulation layer is not this milestone. Do not fake successful operations,
current time or unsupported compiler options. Stop and discuss a genuinely
missing OS facility instead of broadening its interfaces in a port patch.

Keep ELF relocatable objects and archives as compiler/linker inputs. Native
executables must satisfy the existing P1F contract. No new object/archive format,
own assembler/linker or application-bundle work is required. A possible future
linker for application bundles stays outside this milestone.

## First usable compiler

Build TCC itself with the host Pyxis GCC and SDK. The guest compiler targets
x86-64 Pyxis applications, not the GNU C23 kernel or runtime sources. It consumes
their prebuilt startup/libraries, defines `__pyxis__`, and uses the existing
LP64/System V ABI and x87/SSE2 support. Do not claim full C23 support or compiler
self-hosting.

The intended first command set is preprocessing (`-E`), ELF object generation
(`-c`), and static linking of C/ELF objects/archives to P1F, with `-o`, `-I`,
`-D`, `-U`, `-L` and `-l`. Ordinary multiple-input and include handling are part
of making that useful. `-run`, shared objects, runtime loading, bounds checking,
coverage, runtime backtraces, archive creation and compiler subprocess dispatch
are excluded; unsupported requests must produce an error rather than silently
changing behavior. The exact accepted option set is recorded with the recipe.

The completion target is `main`/`puts` compiled and linked inside Pyxis, followed
by editing and rebuilding the same source in Kilo. Floating-point literals and
normal arithmetic must work too; the completed FP support is not a reason to
silently limit TCC to integer programs. libc float formatting and a full libm
remain separate work.

## Audited inputs and evidence

TCC pin: `3dc99dbc82f8e07308c5d398136803e62f9676df` (2026-09-22), identifying
itself as `0.9.28rc`. The upstream `mob` refs at
[repo.or.cz](https://repo.or.cz/tinycc.git) and the
[GitHub mirror](https://github.com/TinyCC/tinycc/tree/3dc99dbc82f8e07308c5d398136803e62f9676df)
matched at audit time; recipes must use the commit, not the branch. Preserve
`COPYING` and individual source notices; the compiler and support-runtime files
have distinct license notices, including the runtime linking exception.

Original audit baseline: Pyxis `d72f15c`, userland `31c01c1`, ports `60387cc`. Built the normal
SDK and userland with the Pyxis GCC 16.2.0 toolchain. Built the unmodified pinned
TCC on the host using:

```sh
./configure --config-backtrace=no --config-bcheck=no --config-semlock=no
make -j4 tcc
```

These commands build a host compiler; they do not make it a Pyxis executable.
No upstream tests, new test programs or boot harnesses were added or run.

Observed using existing application sources and libraries:

- Compiling `userspace/cat/main.c` with TCC's private headers and the SDK fails
  at `stdlib.h`'s `[[noreturn]]`. Replacing only that spelling with
  `__attribute__((noreturn))` in a temporary header copy permits compilation.
  The repository and exported SDK were not edited for this experiment.
- Compiling the existing hello application reaches a missing `stdint.h`.
  Adding GCC's private header directory instead fails in its `stdint.h` at
  `int_least8_t`: the compiler predefined-type contract differs.
- TCC linked both the GCC-built cat object and the TCC-built cat object with
  the existing `crt0.o`, `libc.a`, `libpyxis.a`, then `libc.a` again, using
  `-nostdlib -static -Wl,-Ttext=0x400000,-section-alignment=0x1000`.
  No host startup or libraries were supplied to these target links. The TCC-built
  cat also linked with a single libc/libpyxis pass. Broader archive dependency
  cycles and a permanent rescan policy remain to be reviewed.
- The existing converter rejected TCC's ELF layout. In the GCC-object case,
  the RX segment started at `0x403840`, file offset `0x2840`, despite a
  `0x1000` alignment value. These are congruent ELF offsets, not page-aligned
  segment starts as required by P1F.
- Linking the TCC-produced cat object with GNU ld and the existing Pyxis linker
  script produced an ELF accepted by `elf2pxe`. GNU ld warned that TCC's object
  lacks `.note.GNU-stack`; record non-executable stack metadata in the port.

This establishes useful object/archive interoperability, not complete ABI or
relocation coverage. The experimental output was not booted, and TCC has not
run inside Pyxis. Floating-point calls, variadic callees, aggregate arguments,
compiler helpers and larger inputs still need ordinary build/debugger review
as their implementation slices become runnable.

## Completed prerequisite

As of Pyxis `05d72e5` and userland `df4274f`, x87/SSE2 compiler defaults, libgcc
helpers and Mandelbrot are merged. The owner rebuilt the compiler container and
confirmed the application locally. See the [FP contract](../userspace.md#floating-point).
The kernel retains eager per-task state preservation and integer-only code;
AVX remains disabled. Literal conversion, float formatting and libm are distinct
from hardware arithmetic.

TCC evaluates floating constants in `parse_number` and `gen_opif`; its `-mno-sse`
option does not remove those operations from the compiler itself.

## Runtime and native platform gaps

| Area | Finding and proposed boundary |
| --- | --- |
| Public headers | Use declarations TCC can parse while keeping first-party implementation GNU C23. Provide target-owned integer/limit definitions; use TCC's own compiler-dependent `stdarg.h`/`stddef.h`. Audit `bool` and attributes in public headers. Do not export GCC internals or invent an ABI version. |
| Ordinary libc | Allocation, memory routines, unbuffered streams, seeking, errno and integer formatting exist. Task 3 supplies retained string/integer-conversion requirements. Sorting, assert/abort and formatting call-site work remain in task 4; use existing bounded formatting where appropriate. |
| Numeric conversion | `strtof`, `strtod`, `strtold` and `ldexpl` are used by literal parsing. Select and review an implementation before this task; correct parsing, range handling and long-double behavior are separate from enabling the FPU. No dummy conversions or silent integer-only compiler. |
| Error unwinding | TCC uses `setjmp`/`longjmp` to recover from compile errors. Supply the C facility, including its x86-64 ABI/compiler attributes, without signal-mask or POSIX additions. Define its FP-control behavior with the FP contract. |
| Files | `BufferedFile`, `full_read`, object/archive readers and response files use fd-shaped `open/read/lseek/close`. Adapt the port's actual source to native streams/handles; do not add POSIX syscalls or a public fd layer just for TCC. Preserve short-read/error handling and source ownership. |
| Output | Replace unlink/open/fdopen with create/truncate stream output. Check writes and close. Failed output may remain partial until filesystem replacement/removal support exists; never report success. |
| Paths | Unix `PATHSEP` is `:` and `IS_ABSPATH` recognizes leading `/`. Both conflict with `app://` and `home://`. Treat native URIs as rooted paths and keep individual `-I`/`-L` arguments intact. Settle a search-list convention for configured defaults/environment separately. |
| Include identity | `realpath` is used for `#pragma once`. Native capability paths have no existing realpath API. Choose bounded port-local handling or an explicit unsupported diagnostic; do not claim different path spellings identify different files or add a global VFS interface solely for this. |
| Time | `__DATE__`/`__TIME__` use time/localtime and `-bench` uses gettimeofday. Decide real clock support or explicit diagnostics when these features are requested. Do not fabricate a current date. See [timekeeping debt](../technical-debt.md#timekeeping-beyond-delivered-timer-ticks). |
| Unneeded host services | Exclude JIT/run, dynamic loading, coverage, runtime backtraces, bounds checking, semaphore locking and cross-compiler exec dispatch. Static configuration alone does not exclude all native-run paths. Reject unavailable command options explicitly. Archive creation tools can wait; reading `.a` is needed now. |

The host executable's undefined-symbol list contains optional Linux services
as well as core dependencies. It is evidence for the audit, not a libc shopping
list: removing optional paths should remove their requirements.

TCC's Unix defaults also choose Linux predefines, crt1/crti/crtn and standard
library paths. A Pyxis target must define `__pyxis__`, supply the native `_start`
and libraries, preserve LP64/System V layouts, and avoid those implicit host
defaults. Keep ELF `.o` and `.a` inputs. The SDK linker script's `PHDRS`/`SECTIONS`
language is not supported by TCC's limited linker-script reader.

TCC emits support calls beyond the symbols provided by libgcc. For example,
x86-64 `va_arg` uses its `__va_arg` helper. Build the needed `libtcc1` subset for
Pyxis with its per-file notices; do not assume GCC's libgcc is a replacement.
Audit archive rescan policy, helper selection, long-double ABI and default
library ordering before presenting a supported compiler command.

## Executable output recommendation

Prefer a bounded native P1F writer after the runtime prerequisites, retaining
the existing linker and relocation machinery. This is a source-based assessment,
not a completed patch. In the pinned `tccelf.c`, `elf_output_file` resolves
runtime/common symbols, lays out sections, relocates them and then writes output.
That provides a plausible insertion point without replacing the linker.

Required local work:

- Select P1F output explicitly, retaining ELF relocatable objects and archives.
- Lay out readable R/RX/RW groups on distinct 4 KiB boundaries before relocation,
  with a fixed base and no writable-executable segments. Reuse the existing
  section sorting/layout where possible; the binary-output branch already
  aligns permission transitions differently from ELF.
- Resolve `_start`, reject unresolved symbols and unsupported TLS/dynamic
  requirements, then emit the existing P1F header, segment descriptors and bytes.
  Account for section gaps and zero-filled tails; validate range/size overflow.
- Do not send P1F through the raw binary writer, which loses the entry point,
  permissions and zero-fill metadata. Retain its actual format contract.

ELF plus guest `elf2pxe` remains the fallback. It still needs a TCC layout change
to satisfy current segment alignment, and the converter needs target ELF/endian
definitions currently supplied by host headers. Its stream/allocation operations
mostly exist already. Do not weaken P1F or its converter to accept the current
unaligned output. Stop for discussion if either path needs broad linker changes.

## PR worklist

Tasks 2–6 are shared userland/SDK work; 7–10 are primarily TCC port work;
11–12 integrate the guest tools. Dependencies below are technical prerequisites,
not permission to start unresolved tasks. Keep each item a focused PR (with a
companion submodule-pin PR where needed); split an item again if its source
review reveals a larger change. Update its checkbox when delivering it.

1. [x] FP support and Mandelbrot.
2. [x] Public headers usable by TCC and GCC.
3. [x] String and integer-conversion libc facilities.
4. [x] Sorting and diagnostic libc facilities.
5. [x] C nonlocal jumps.
6. [ ] Floating literal conversion and binary scaling.
7. [ ] Pyxis code-generation defaults and compiler support archive.
8. [ ] Native file I/O and URI handling in TCC.
9. [ ] Guest compiler driver and build recipe through object output.
10. [ ] Native P1F executable output.
11. [ ] Guest SDK and compiler packaging.
12. [ ] Complete the interactive edit/build/run loop.

### 2. Public headers

Complete: userland owns SDK `stdint.h`/`limits.h`, preserving the Pyxis GCC
integer types, limits and constant suffixes, including the existing fast types.
The agreed single-byte libc limit is `MB_LEN_MAX=1`. `stddef.h`, `stdarg.h`,
`stdbool.h` and `float.h` stay compiler-provided; TCC's C11 `stddef.h` supplies
`max_align_t`. SDK includes take precedence over compiler includes.

Public exit declarations use `__attribute__((noreturn))`; `startup.h` includes
`stdbool.h` directly. No TCC-specific branches, kernel changes or ABI layout
changes were needed. Existing `_Static_assert` and diagnostic attributes remain.
Cat and Mandelbrot compile with pinned host TCC and the real SDK without host
system headers or temporary replacements. The ordinary GCC image build and
boot remain the integration check. See the [SDK contract](../sdk.md) for commands;
this is header consumption, not complete generated-code interoperability.

### 3. Strings and integer conversion

Complete: native userland libc implementations of `strcpy`, `strtol`, `strtoul`,
`strtoll`, `strtoull` and `atoi`, with `ERANGE` and its `strerror` message. The
integer conversions handle ASCII whitespace/signs, bases 2–36 and base 0,
including C23 binary prefixes, end pointers and range errors. Existing allocation,
streams and memory routines remain authoritative. No imported source, kernel
calls or TCC-specific libc behavior.

The pinned source uses these calls in compiler strings, assembler numeric
operands, ELF archive sizes and command-line parsing. `strpbrk` occurs in the
archive tool and Mach-O backend, which are outside this port's selected scope;
it was a candidate in the audit, not a retained requirement, and stays deferred.
The changed libc sources compile with GCC and pinned host TCC against the SDK.
Full compiler-unit builds still depend on the later runtime/platform tasks;
this does not claim a linked or runnable guest TCC.

### 4. Sorting and diagnostics

Complete: userland libc supplies an iterative, allocation-free heapsort for
`qsort`, `<assert.h>` and `abort`. Assertions report the expression, file, line
and function to stderr without allocating, then abort. `NDEBUG` suppresses
evaluation and is reconsidered on each header inclusion. Abort uses
`_Exit(EXIT_FAILURE)` without libc cleanup or signals. These are shared libc
facilities; the kernel and native ABI are unchanged.

The pinned compiler uses `qsort` for switch-case ordering, assertions throughout
the x86-64 generator, and `abort` in its x86-64 varargs support. The new sort and
diagnostic sources compile with GCC and pinned host TCC against the SDK. Full
compiler builds still need the later runtime and platform tasks.

Formatting review for the port patches in tasks 7–9: keep the existing integer
`snprintf`/stdio implementation; no `sprintf` or floating printf addition is
needed for the retained calls. Replace calls with explicit destination bounds
and check formatting failure/truncation rather than accepting partial names:

- `tccpp.c:get_tok_str`: three numeric formats (`%llu`, `<\\x%02x>`, `L.%u`)
  write at the start of `cstr_buf`, allocated during preprocessor initialization.
  Use its `size_allocated`, not the size of the data pointer.
- `tccgen.c:parse_atomic`: format the helper name into `buf[40]` with `sizeof(buf)`.
- `tccasm.c:asm_parse_directive`: two section-name formats use `sname[64]`;
  use `sizeof(sname)`.
- PE name decoration, archive creation in `tcctools.c`, and the Windows backtrace
  DLL are excluded from this port; their `sprintf` calls need no libc expansion.

The formatting patches belong with the compiler port, whose sources and recipe
are not imported yet. This task records the call-site decisions, not a runnable
guest compiler.

### 5. Nonlocal jumps

Complete: userland libc supplies `jmp_buf`, the `setjmp` macro/declaration and
`longjmp`, with returns-twice/noreturn compiler attributes and x86-64 assembly.
The eight-word buffer holds RBX, RBP, R12–R15, the post-return RSP and RIP;
zero passed to `longjmp` becomes one. The public header and
[runtime contract](../userspace.md#foundational-libc) describe valid call sites,
frame lifetime and changed automatic locals. No allocation or kernel changes.

The FP decision follows C23: leave the x87 and MXCSR environment as it exists
at the `longjmp` call, including control modes and status. Do not restore the
environment from `setjmp`, or save caller-saved FP data registers. This is a
nonlocal C return, not a scheduler context switch or signal-mask operation.

Pinned TCC's `error1` frees the diagnostic string and tracked temporary
allocations before jumping to `tcc_compile`. Its recovery path calls
`tccgen_finish` and `preprocess_end` on either outcome: they release compiler
state/macros, close the `BufferedFile` chain and free its buffers. Task 8 must
retain that ownership path when replacing file descriptors with native streams;
`longjmp` itself does no cleanup.

The ordinary SDK/image build includes the assembly. GCC and pinned host TCC
also compile the existing `libtcc.c` recovery call sites with just the new
`setjmp.h` substituted into the host build's includes; those inspection objects
are not linked or packaged as guest code. GCC honors `returns_twice`; pinned
TCC ignores that attribute but already spills live expression registers across
calls. Generated calls target `setjmp` directly, without a C wrapper whose
frame would expire. Runtime inspection of an actual guest compile/error path
remains in task 9.

### 6. Floating literal conversion

Owner: userland libc. Depends on 1–2. Split into two focused PRs:

- [x] Math support: `ldexpl`, `scalbn`, `scalbnl`, `fmodl`, `fabsl`, `copysignl`.
- [ ] String conversion: `strtof`, `strtod`, `strtold`.

Agreed source: a narrow musl subset, pinned to release 1.2.5 commit
`0784374d561435f7c787a555aeab8ede699ed298`, with its MIT license and notices.
Math sources retain upstream algorithms and formatting; a small private header
supplies their x86-64 long-double layout. They live in libc with a minimal public
`math.h`; callers do not acquire a separate libm link dependency. Math errors
follow musl's FP-exception convention, leaving `errno` unchanged. The build uses
rounding-aware compilation and standard excess precision. Provenance and local
adaptations are recorded in userland's `third_party/musl/UPSTREAM.md` and shipped
with the SDK.

Next: adapt musl's conversion scanner to read a string without importing its
FILE implementation. Use the existing float/double/80-bit long-double ABI with
precision-specific conversion. Retain the numerical algorithm, covering decimal
and hexadecimal forms, rounding, overflow/underflow, end pointers, errno and
special values. Syntax is ASCII with `.` as decimal separator; no locale state.
Do not import the rest of libc/libm or add floating-point printf.

Done: target libraries build and export the required conversion/scaling
facilities without host math or parser stubs. The compiler can consume their
declarations. Guest compile/debugger inspection of ordinary FP expressions
follows when 9–10 are runnable.

### 7. Pyxis target defaults and compiler support

Owner: ports/TCC patches and pinned provenance. Depends on 2 and 4; FP baseline
is already available. Keep the selected upstream commit and its licenses.

Separate the platform on which TCC itself runs from the platform it generates
code for. Define a Pyxis target with LP64/System V conventions, `__pyxis__`,
baseline x87/SSE2, native startup/library choices and no Linux predefines. Keep
ELF `.o`/`.a` interoperability and non-executable-stack metadata. Build the
required `libtcc1` subset, including x86-64 varargs support, using the target SDK.
Audit its overlap with libgcc and decide library order/archive rescanning rather
than allowing duplicate runtime definitions to win accidentally.

Done: a host-built TCC configured for the Pyxis target compiles existing
application sources to ELF objects using the real public headers. GNU ld and
the existing converter may supply the temporary host-side link path for ordinary
boot/debugger inspection. Inspect ABI boundaries in actual programs, including
variadic calls, aggregate returns and FP use; do not infer full ABI coverage
from a successful link. This host tool is intermediate evidence, not the guest
compiler or a host dependency of the final editing loop.

### 8. Native streams and paths

Owner: ports/TCC patches. Depends on 3–4 and 7.
Adapt `BufferedFile`, `full_read`, archive/object readers and output creation to
native streams/handles. Preserve binary reads, seeking, short-read/error handling
and ownership on normal and nonlocal exits. A host file descriptor must not be
confused with a Pyxis capability handle. No SDK-wide fd emulation.

Resolve relative paths through the inherited working context; treat native
URIs as rooted paths. Retain directory-relative includes and explicit `-I`/`-L`
arguments without splitting `app://` at a colon. Review response files and
linker-script path handling in the same pass; reject unsupported forms clearly.
Use existing create/truncate writes and check close errors. A failed compile
may leave partial output until independent replacement/removal support exists.

Before implementation: settle configured search lists and include identity.
Prefer repeated explicit paths over adding a new global environment convention.
For `#pragma once`, propose a bounded port-local URI normalization only if it is
correct for the supported namespace; aliases cannot be assumed identical or
distinct from path spelling alone. Otherwise report the limitation explicitly.
Do not add `realpath`, `stat` or new filesystem semantics just for this port.

Done: affected compiler units build against native headers, with no implicit
POSIX I/O dependencies. Record remaining driver dependencies for 9; the full
compiler need not yet link. Keep unfinished patches out of the normal image.

### 9. Guest driver and object compilation

Owner: ports/TCC recipe, metadata and patches. Depends on 3–8.
Build the compiler as a freestanding Pyxis executable with the prebuilt GCC/SDK,
following the existing Lua port recipe model. Remove optional host services,
retain native diagnostics and error unwinding, and make `-E`/`-c` usable. Record
exact supported options and required startup grants (console, working context,
readable source/header roots, writable output directory and memory).

Before implementation: decide the current-time policy. Until wall-clock support
exists independently, the proposed behavior is a clear error when `__DATE__`,
`__TIME__` or unavailable benchmark timing is requested, not a fabricated date
or a TCC-specific clock syscall. Review stack use against the existing 64 KiB
process stack. If that exposes a real general runtime need, discuss it; do not
silently enlarge kernel resources for the port.

Done: manually launch the staged compiler in Pyxis, compile a source file to an
ELF object in `home://`, and observe useful diagnostics and resource cleanup on
an ordinary source error. Do not claim successful executable linking yet.
Temporary image staging can provide the required headers; permanent packaging
is 11. Keep the reusable build recipe in ports, not the kernel build scripts.

### 10. Native executable output

Owner: ports/TCC linker patches. Depends on 7–9.
Implement the [bounded P1F output path](#executable-output-recommendation): fix
layout before relocation, write entry/segment metadata and contents, preserve
zero-fill tails and reject unresolved symbols or unsupported dynamic/TLS needs.
Do not alter the P1F ABI, loader permissions or alignment rules.

Before implementation: confirm the audited insertion point still gives a small
local change. Use the existing linker; if it requires broad restructuring,
stop and compare the ELF-plus-guest-converter fallback. A host conversion is
never the final path.

Done: an in-guest compile/link emits a PXE that the unchanged loader launches.
Inspect the segment layout and run ordinary integer/FP application code linked
with the GCC-built runtime and the chosen TCC/libgcc helper set.

### 11. Guest SDK and compiler packaging

Owners: ports stages and Pyxis build/initrd integration. Depends on 9–10.
Proposed location: read-only `app://sdk`. Agree the final paths before coding.
Package compiler-private headers separately from shared Pyxis headers, `crt0.o`,
libc/libpyxis/libterm, libtcc1 and the required target libgcc archive, together
with their licenses and source provenance. The guest payload is target runtime
material, not the host GCC/binutils installation or host `elf2pxe` binary.

Wire the compiler and SDK into the existing explicit initrd entry list and
incremental build inputs. Native TCC search defaults must find only these guest
resources; `-I`/`-L` can select additional explicitly granted paths. Keep writable
source/output under `home://`. Record source/helper revisions, not a new ABI
version. No new CI framework, dependency resolver or package manager.

Done: an ordinary clean image build contains the compiler and complete guest
runtime inputs. A fresh boot compiles and launches a program without manually
copying host files into the guest.

### 12. Finish the loop and documentation

Depends on 11. In Kilo, write a normal C `main` using `puts`, save it, compile to
PXE and launch from the shell. Edit its output and repeat without rebuilding the
ISO. Intended commands from `home://` (not available until this work is complete):

```text
kilo hello.c
tcc hello.c -o hello.pxe
./hello.pxe
```

Exercise source/header paths and ordinary error diagnostics through the same
tools; inspect actual memory/stack use rather than claiming all inputs fit.

Done: document the working commands, supported language/options, grants, paths
and remaining limits. Move completed milestone material from WIP into concise
implemented-behavior documentation; preserve relevant deferred work in WIP or
technical debt. Git retains this plan. Validation throughout uses ordinary
builds, interactive boots and debugger inspection, with no added tests,
self-tests, fault injection or boot/output automation.

## Pinned source references

- [Core configuration and native defaults](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/tcc.h).
- [Driver, paths, file input and error recovery](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/libtcc.c).
- [Literal parsing and date/time macros](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/tccpp.c).
- [Floating constant folding](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/tccgen.c).
- [Layout, relocation, output and archive loading](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/tccelf.c).
- [x86-64 code generation](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/x86_64-gen.c) and
  [varargs helper](https://github.com/TinyCC/tinycc/blob/3dc99dbc82f8e07308c5d398136803e62f9676df/lib/va_list.c).
- [P1F contract](../../include/pxe/p1f.h), [converter](../../tools/elf2pxe.c),
  [SDK](../sdk.md) and [ports integration](../ports.md).
