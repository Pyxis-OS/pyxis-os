# LLVM toolchain on the host

Pyxis builds with Clang, LLD and the LLVM archive and object tools, running on
the host and targeting `x86_64-unknown-pyxis`. GCC and binutils are retired.
The milestone completed on 2026-10-07 with QEMU checks and the owner's ThinkPad
check. It is the first of the three
[LLVM milestones](../wip/toolchains-and-runtimes.md#llvmclang-transition-and-hosting).
[C++ in userspace](cxx-userspace.md) followed; Clang hosted on Pyxis comes next.

The [toolchain README](../../toolchain/README.md) is the reference for building
the toolchain, its target contract and its licenses. The [SDK](sdk.md) describes
how applications use it.

## The fork

- **Repository:** [`PyxisOS/pyxis-llvm`](https://git.internal/PyxisOS/pyxis-llvm),
  forked from LLVM's `release/23.x`; upstream sources come through the
  `mirrors/llvm-project` mirror. The `pyxis-23.1.3` branch holds the Pyxis commits on top of `llvmorg-23.1.3`,
  and changes reach it as PRs.
- **The Pyxis commits:**
  - the Pyxis OS in LLVM's triple, Clang's target information and its driver:
    `__pyxis__`, no red zone, LP64 only, no shared or PIE output, and
    `crt0.o`, `pyxis.ld`, libc and libpyxis from the sysroot;
  - compiler-rt's hand-written `__floatundixf` kept off the red zone;
  - `--oformat=p1f` in LLD, which the driver passes for every executable link.
    An executable without `-o` is `a.pxe`;
  - for C++, the sysroot's libc++ headers ahead of libc's, libc++, libc++abi
    and libunwind in the link, and `--eh-frame-hdr` for every executable;
  - thread-local storage rejected when compiling;
  - libc++ using `timespec_get` for its clocks, no terminal check in `print`,
    and no `ELAST` limit.
- **The pin:** `toolchain/build.sh` fetches one commit shallowly, currently
  `49e2c1a1518b`. The SDK builds its [C++ runtime](sdk.md#c) from the same
  commit. Moving to a new LLVM release is a rebase onto its tag in a new
  `pyxis-VERSION` branch.

## Changing the pin

A fork change or a new release needs, in one Pyxis PR:
1. the new commit in `toolchain/build.sh`;
2. the same commit in `ci/Containerfile`'s check;
3. a new image tag, `pyxis-llvmVERSION-COMMIT`, in `.forgejo/workflows/build.yml`
   and the toolchain README.

The owner builds and publishes that image before CI can pass. Ordinary changes
to the SDK, userland or ports need no new image. A local build directory made
with another compiler stops with a `make clean` request; `build/toolchain`
records the compiler. A new commit under the same compiler name does not, so
run `make clean` after installing it.

## P1F output

LLD lays out the fixed-address ELF image in memory, then writes its loadable
segments and entry point as P1F, with the loader's checks: readable,
page-aligned, ordered segments, none both writable and executable, no TLS,
interpreter or dynamic segment, and an entry inside an executable segment.
Before `elf2pxe` was removed, LLD's output was byte-identical to its conversion
for all 50 executables then built.

The SDK, userland and ports link `.pxe` files directly; no build keeps an ELF.
`-Wl,--oformat=elf` gives an ELF with symbols at the same addresses, for a
debugger; the kernel link uses it because Limine loads ELF. `include/pxe/p1f.h`
defines the format. A change to it must update the kernel loader, the fork's
`lld/ELF/P1F.cpp` and the TCC port's writer together.

## Behaviour and performance

The ABI, the kernel's register restrictions and the userspace CPU-state
assumptions are unchanged. Clang found one real problem in Pyxis code: unread
PS/2 mouse counters, now `volatile`. Both linker scripts write `PHDRS` flags as
numbers, because LLD rejects symbols there.

Clang narrowed TLSF's flag updates to one-byte stores, which stalled the
whole-word loads that follow. Both TLSF copies now write the size word in full;
see [TLSF provenance](../../third_party/tlsf/UPSTREAM.md).

Matched measurements on 2026-10-07, GCC main `70a4e20` against LLVM `a01bd0e`,
in Claude's nested-KVM VM with four CPUs:

| Measurement | GCC | LLVM |
| --- | --- | --- |
| QEMU start to `Caelum ready` (s) | 1.929, 1.971, 1.937 | 1.803, 1.872, 1.853 |
| `iobench read boot://…`, payload median (MiB/s) | 3995, 3889, 3970 | 4389, 4624, 4655 |
| `iobench write tmp://…`, median (MiB/s) | 121.8, 107.1, 126.1 | 95.8, 136.1, 153.3 |
| `allocbench heap` (ns/op, 12 samples) | 11.8–12.4, median 12.0 | 12.9–13.3, median 13.1 |
| `allocbench pages` (ns/op) | 38711, 38050, 38599 | 38066, 38191, 38082 |
| Quake `timedemo demo1` (fps, 5 samples) | median 1604.2 | median 1560.3 |
| `pyxis.iso` (bytes) | 53,897,216 | 46,356,480 |
| Kernel text (bytes) | 820,981 | 811,381 |
| 51 executables (bytes) | 8,414,768 | 8,495,599 |

Boot archive reads are faster with the LLVM kernel; the heap and Quake are
slower. The ISO shrinks mostly through smaller debug information in the guest
SDK's archives. Clean kernel builds take about half as long and SDK builds about
two-thirds; the toolchain itself builds in about 8 minutes on 8 vCPUs. TCC's guest compiler is
about 31% larger because Clang inlines more in its single-translation-unit
build.

On the owner's ThinkPad, an all-LLVM PXE build ran normally. Quake
`timedemo demo1` on AC gave about 660–690 fps over several runs, level with the
last GCC figure of 667.1 fps. On battery it falls to about 400 fps and console
drawing slows, because the firmware throttles the CPU; Pyxis does no
[frequency control](../wip/later-os-directions.md#power-and-acpi). State AC or
battery for native performance figures, and compare on AC.

## Limits

- [Clang code generation and predefines](../technical-debt.md#clang-code-generation-and-predefines):
  the remaining heap and Quake differences, extra byte-sized read-modify-writes,
  and Clang's fast-type predefines, which differ from libc's `stdint.h`.
- compiler-rt's `cpu_model` stays in the builtins archive. A program that uses
  `__builtin_cpu_supports` links it, and libc's startup runs its constructor
  before `main`.
- Go's linker output, converted by the removed `elf2pxe` in the
  [Go investigation](../wip/go-runtime.md), now needs P1F output from Go's
  linker or a restored converter.
