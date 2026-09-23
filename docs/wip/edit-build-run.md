# Edit, build and run inside Pyxis

Status: desired milestone, not an implementation assignment. Prefer Kilo as the
first guest port, then assess TCC against this concrete outcome. Guest Lua is
not a prerequisite. Define focused stages as their requirements become clear.

## The complete loop

Inside Pyxis, create or edit a C hello-world source file in Kilo, save it, compile
it with TCC, then launch the resulting PXE executable from the shell and see its
output. Repeat after changing the source. Editing, compilation, any conversion
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
2. **TCC feasibility and target contract.** Pin TCC, audit its host-service and
   C-library needs, inspect target header compatibility, and verify linking of
   the existing startup objects and runtime archives. TCC need not rebuild the
   GNU C23 kernel or runtime; compiling applications against their public headers
   and linking their built libraries is the initial requirement.
3. **In-guest compilation.** Port the required compiler services and selected
   output path in focused PRs. Completion is TCC producing a launchable hello
   program within Pyxis, with useful compile/link failure diagnostics.
4. **Join the loop.** Use Kilo, TCC and the shell together on a writable source
   file, document the small set of commands, and verify repeated edit/build/run
   cycles through ordinary interactive boots.

TCC's priority serves this loop; it does not establish that the compiler is an
easier port than Lua. Reassess after the feasibility stage if runtime or linker
work grows beyond a manageable slice.

## Bounded native PXE output investigation

TCC's documented linker handles ELF objects and archives and can emit linked
executables without an external linker. In the inspected upstream source,
section layout and relocation precede final file writing; the output path
already distinguishes ELF and raw binary output. This suggests a local P1F
writer may be feasible, but is not a completed compatibility assessment.

Investigate direct PXE/P1F executable output while preserving ELF object/archive
inputs and existing compiler/linker machinery. Check entry selection, final
relocations, zero-filled memory, startup/runtime helpers and segment layout.
P1F requires page-aligned nonoverlapping segments, a valid executable entry and
no writable-executable segment. Raw binary output alone does not encode these
requirements; adding a header to arbitrary bytes is insufficient.

Completion of the investigation is a concrete patch boundary and known gaps,
not a compiler rewrite. If correct output needs broad linker restructuring,
new relocation machinery or executable-format redesign, stop and discuss it.
Do not weaken the loader or change P1F just to accommodate compiler output.

A proposed fallback is TCC emitting compatible static ELF followed by a Pyxis
build of elf2pxe inside the guest. That still meets the edit/build/run goal;
native TCC output remains desirable if the patch stays focused. Even this path
needs verification of layout compatibility and converter runtime requirements.
A hidden host conversion would not complete the milestone.

## Scope limits

No dynamic linking, JIT/`tcc -run`, full toolchain self-hosting, package manager
or kernel rebuild inside Pyxis is required. Do not broaden the milestone to all
C language features or ports. Choose the supported initial TCC configuration
and application subset explicitly after the audit.

## References

- [TCC manual: linker and output formats](https://bellard.org/tcc/tcc-doc.html).
- [Upstream output/layout implementation](https://github.com/TinyCC/tinycc/blob/mob/tccelf.c)
  (initial inspection; pin a revision for the feasibility work).
- [P1F contract](../../include/pxe/p1f.h) and
  [current converter](../../tools/elf2pxe.c).
- [SDK milestone](../sdk-and-repositories.md) and [port recipes](../ports.md).
