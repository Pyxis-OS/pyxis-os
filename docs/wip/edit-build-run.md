# Edit, build and run inside Pyxis

Status: Kilo is integrated, the [TCC feasibility audit](tcc-port.md) is recorded,
and [compiler selection remains open](guest-compiler.md). Compiler implementation
remains proposed work. Guest Lua is not a prerequisite; discuss each slice's
remaining decisions before implementing it.

## The complete loop

Inside Pyxis, create or edit a C hello-world source file in Kilo, save it, compile
it with the selected guest compiler, then launch the resulting PXE executable
from the shell and see its output. Repeat after changing the source. Editing,
compilation, any conversion
and execution must happen in the guest, without a host build or ISO rebuild
between iterations.

Ship target headers, startup objects and libraries where the compiler can read
them. The first program should use the Pyxis C runtime, for example a normal
`main` and `puts`, rather than bypassing it with handwritten syscalls. Read-only
SDK resources and writable source/output storage can be separate grants.
Persistent storage is useful but not a prerequisite for demonstrating this loop.

## Proposed stages

1. **Kilo editing — complete.** The pinned [Kilo port](../ports.md) is packaged
   in the normal boot archive and supports opening, editing, saving and reopening
   source files with the shared terminal and libc facilities.
2. **TCC feasibility audit — complete.** The [pinned audit](tcc-port.md) records
   header/runtime gaps, successful host-side linking of current runtime archives,
   incompatible default ELF segment alignment and the proposed native P1F path.
   This is not in-guest compiler or complete ABI validation. TCC need not rebuild
   the GNU C23 kernel/runtime; it should compile applications using their public
   headers and link their prebuilt libraries.
3. **FP support and Mandelbrot.** Review the existing eager x87/SSE task state,
   reconcile userspace build settings and demonstrate hardware floating point
   with terminal background colors. Keep kernel code integer-only and defer AVX.
   This comes before the remaining compiler port; details are in the audit.
4. **Compiler selection and in-guest compilation.** Choose a compiler using the
   [whole-pipeline comparison](guest-compiler.md), then port its required services
   and output path in focused PRs. Completion is producing a launchable hello
   program within Pyxis, with useful compile/link failure diagnostics.
5. **Join the loop.** Use Kilo, the compiler and the shell together on a writable
   source file, document the small set of commands, and verify repeated edit/build/run
   cycles through ordinary interactive boots.

TCC was the initial candidate, not a requirement. Reassess if runtime or linker
work grows beyond a manageable slice; another frontend still needs a complete
guest path from source to launchable image.

## Bounded native PXE output investigation

For TCC, the [audit's recommendation](tcc-port.md#executable-output-recommendation)
is a bounded P1F writer after its existing layout/relocation stage, with page-aligned
permission groups and unchanged ELF object/archive inputs. It identifies the
patch boundary and required checks; no implementation is complete yet.

ELF followed by a guest elf2pxe remains the fallback. It also needs a layout fix:
the audited TCC emits unaligned segment starts that the current converter rightly
rejects. Do not weaken the loader or format to accommodate that output. Stop for
discussion if either path needs broad linker restructuring. A hidden host
conversion would not complete the edit/build/run milestone.

## Scope limits

No dynamic linking, JIT/`tcc -run`, full toolchain self-hosting, package manager
or kernel rebuild inside Pyxis is required. Do not broaden the milestone to all
C language features or ports. Choose the supported initial TCC configuration
and application subset explicitly after the audit.

## References

- [TCC manual: linker and output formats](https://bellard.org/tcc/tcc-doc.html).
- [Pinned audit and conditional worklist](tcc-port.md), plus
  [compiler alternatives](guest-compiler.md).
- [P1F contract](../../include/pxe/p1f.h) and
  [current converter](../../tools/elf2pxe.c).
- [SDK milestone](../sdk-and-repositories.md) and [port recipes](../ports.md).
