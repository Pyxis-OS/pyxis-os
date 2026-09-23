# TCC feasibility and implementation slices

Status: source audit and host build observations, with proposed implementation
tasks. No TCC port, runtime changes or new executable format are implemented by
this document. Discuss each task's unresolved choices before implementing it.
This supports the [edit/build/run milestone](edit-build-run.md). Compiler
selection remains open; see the [candidate comparison](guest-compiler.md).
The TCC-specific tasks below apply only if it is selected.

## Audited inputs and evidence

TCC pin: `3dc99dbc82f8e07308c5d398136803e62f9676df` (2026-09-22), identifying
itself as `0.9.28rc`. The upstream `mob` refs at
[repo.or.cz](https://repo.or.cz/tinycc.git) and the
[GitHub mirror](https://github.com/TinyCC/tinycc/tree/3dc99dbc82f8e07308c5d398136803e62f9676df)
matched at audit time; recipes must use the commit, not the branch. Preserve
`COPYING` and individual source notices; the compiler and support-runtime files
have distinct license notices, including the runtime linking exception.

Pyxis baseline: `d72f15c`, userland `31c01c1`, ports `60387cc`. Built the normal
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

## Floating point before the compiler port

The owner proposed a small Mandelbrot application as the first consumer of
supported userspace floating point and terminal escape colors.

The kernel already has eager x87/SSE preservation:
[arch_user_init/save/restore](../../arch/x86_64/user.c) initializes BSP/AP control
state, initializes each new task's clean state, and uses FXSAVE64/FXRSTOR64.
[Task scheduling](../../kernel/task.c) saves on user preemption and blocking
waits and restores before dispatch. The aligned state belongs to each user
task. AVX remains disabled and kernel code must remain free of FP/SIMD use.

The compiler now defaults to baseline x87/SSE2 and builds the normal x86 libgcc
helpers. SDK/runtime/application builds use that baseline; kernel builds retain
`-mgeneral-regs-only`. This requires rebuilding the compiler/container. See the
[implemented FP contract](../userspace.md#floating-point).

`mandelbrot` is an ordinary userspace application using `double` arithmetic and
SGR background colors. It is included in the boot archive and can be launched
from the shell. libc conversions, `%f` formatting, libm and AVX remain deferred.
TCC itself evaluates floating constants in `parse_number` and `gen_opif`;
disabling generated SSE with its `-mno-sse` option does not remove these operations.

## Runtime and native platform gaps

| Area | Finding and proposed boundary |
| --- | --- |
| Public headers | Use declarations TCC can parse while keeping first-party implementation GNU C23. Provide target-owned integer/limit definitions; use TCC's own compiler-dependent `stdarg.h`/`stddef.h`. Audit `bool` and attributes in public headers. Do not export GCC internals or invent an ABI version. |
| Ordinary libc | Allocation, memory routines, unbuffered streams, seeking, errno and integer formatting exist. Core compiler paths also use `strcpy`, `strpbrk`, integer `strto*`, `atoi`, `qsort` and `sprintf`, plus assert/abort support. Add useful C functions in focused slices; replace bounded port-local formatting with existing `snprintf` where appropriate. |
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

## Focused task order

Each item is a separate reviewable slice; split further if its implementation
requires choosing a new runtime facility. These are proposals, not blanket
implementation approval.

1. [x] **FP support and Mandelbrot.** Enable compiler defaults, libgcc and SDK
   settings; retain eager task preservation and the integer-only kernel. The
   terminal application and supported contract are documented above.
2. [ ] **TCC-readable public headers.** Settle ownership of integer/limit headers
   and declaration spellings. Build existing applications with TCC against the
   exported SDK, with no temporary replacements or host includes.
3. [ ] **Integer/string/sort libc prerequisites.** Inventory the selected native
   compiler paths and add only the useful standard functions actually needed.
4. [ ] **C nonlocal jumps.** Add the agreed setjmp/longjmp contract and review
   unwind/resource behavior for TCC diagnostics.
5. [ ] **Floating literal support.** Choose conversion/math implementation and
   pin any reused sources/licenses. Provide the subset TCC needs, not a complete
   libm or floating printf milestone.
6. [ ] **Native compiler I/O and driver.** Pin the recipe, adapt files and URI
   paths, remove optional host services, decide clock/include-identity behavior,
   and run compilation to ELF objects inside Pyxis. No host libc in the guest.
7. [ ] **Runtime link and native output.** Build target helper objects, establish
   startup/library ordering and implement the bounded P1F output path, or discuss
   the fallback if needed. Verify the existing runtime and application ABI.
8. [ ] **Package and join the loop.** Agree the read-only guest SDK path (proposal:
   `app://sdk`) and contents, including compiler-private headers, public headers,
   crt0 and runtime/helper libraries/licenses. Package TCC; edit, compile, launch
   and edit again entirely in Pyxis. Document failures and remaining limits.

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
