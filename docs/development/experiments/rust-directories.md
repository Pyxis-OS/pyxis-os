# Rust native directories

2026-10-26. This directory stage delivers std enumeration, retained-parent entry
queries, native create/remove/rename and checked recursive creation. It does not
close the [first std profile](../../wip/rust-std.md) or implement recursive
removal/copy. No kernel ABI, new grant, compiler-container change or native
hardware run is involved.

## Revisions and build

The baseline was captured before implementation on Pyxis `aa48e42b`,
userland `e80f50f8`, Rust `7bb492df` and libc crate `94498eae`.
The candidate uses userland `156924a0` ([#392](https://git.internal/PyxisOS/pyxis-userland/pulls/392)),
libc crate `bd180991` ([#5](https://git.internal/PyxisOS/pyxis-libc/pulls/5)) and
Rust `4be8bb57` ([#6](https://git.internal/PyxisOS/pyxis-rust/pulls/6)).
The userland helper/runtime changes were first measured at `3ec90eaf` and
rebased unchanged onto userland main `9ad9d15` (package/gamepad changes).
Integration repeats the functional checks after rebuilding on main `1c71d1bb`
and again on `3e5c22e0` after the gamepad/USB merge.
Ports remain `4f2e8aeb`, fs `f3f22b6e`, lwIP `d52ebd20`.

Build the runtime/SDK with
`pyxis-builder:pyxis-llvm23.1.3-3d69e83`, then rebuild the fork's stage-2
Linux-host rustc and host/Pyxis libraries using its pinned bootstrap and CI LLVM,
closed Cargo proxy and offline build. The exported prefix's receipt names
`4be8bb57`; libc overlays and locks name `bd180991`. SDK interfaces, actual C
signatures and flexible dirent layout are checked against the Rust bindings.
The artifact audit verifies receipt hashes and all 26 native rlibs (506 ELF
members), with no embedded LLVM bitcode. Rebuild std main, managed workers,
core+alloc hello, tiny-skia and the existing hexyl port with this prefix/SDK.

Raw commands, private qualification sources, build logs, fixture hashes, GDB
observations and measurements are retained under
`/shared/present/rust-directories/`. These are ordinary qualification programs;
no test framework, boot self-test or CI job was added.

## Functional qualification

QEMU q35/KVM, host CPU, 2 GiB RAM, OVMF, VirtIO GPU/network/RNG and read-only
boot archive, separately with one and four CPUs. A confined virtio-fs directory
supplies HOST; home is RAM and scratch is a disposable RAM-backed npfs volume.
All selected cases pass on RAM, HOST and npfs:

- Enumeration owns names and cached types across cursor advancement and iterator
  drop. RAM/HOST preserve invalid UTF-8 name bytes; npfs truthfully refuses such
  names under its format's UTF-8 rule. RAM accepts the 1,024-byte test component;
  npfs/HOST refuse it and accept the selected 192-byte name.
- Entry metadata remains attached to the original parent across cwd changes,
  alias rebinding and iterator drop. RAM/npfs parent rename also preserves it.
  A removed child reports NotFound; replacement is observed by fresh metadata,
  while the cached type remains the enumeration observation.
- A live RAM/npfs enumeration interrupted by mutation reports EAGAIN once and
  then stays exhausted. HOST reports the actual local change result; arbitrary
  external HOST changes have no new detection guarantee.
- Four Rust workers query owned entries during iterator advancement and after
  its drop. ENUMERATE-only listing and cached types work; entry metadata needs
  the existing child metadata authority. A LOOKUP-only child type query asks
  for no FILE content rights. Failure leaves C helper outputs unchanged.
- Creation, exclusive duplicates, empty/root/missing paths, ordered separators,
  dots and dotdot, boundary refusal, nonempty removal, file replacement and
  held-file lifetime pass. Recursive creation captures one root/cwd, checks
  actual directories and can leave a created prefix on failure.
- HOST symlinks report their cached link type without being followed; special
  entries do not become invented regular files. HOST directory rename and
  cross-store rename report Unsupported. No copy/delete fallback occurs.
- Scoped GDB observes zero provider OPEN/REMOVE calls through directory
  refusals, followed by a successful read-only provider OPEN control. It also
  observes one retained DIR close at the final entry drop and exactly one root
  capture during recursive creation.
- Existing writable File/rights/append-refusal checks pass on RAM and npfs.
  Std main, managed workers/TLS/synchronization, core hello and tiny-skia pass
  (`d0dfb9f8f80d6c59`). Rebuilt hexyl's ASCII/no-colour output matches the pinned
  Linux build byte-for-byte.

Recursive removal and copy reject existing, missing and self-copy cases before
changing sentinel contents. They are the next assigned-profile work item for
Cargo, tempfile cleanup and installers; this stage does not select the shared
fallbacks. No allocator fault injection, durability proof, physical concurrent
HOST writer or mounted-volume retirement run is claimed. The debugger proves
last-owner close; the resulting possibility of holding mount release BUSY is a
reference-lifetime consequence, not a separately measured FAT retirement result.

## Matched ordinary-path controls

Baseline A and candidate B use identical kernel bytes, configuration and archive
file inventory. Only selected existing native executables and the same C control
program are rebuilt. The cost archive grows 15,872 bytes, from 44,386,304 to
44,402,176. Extra Rust qualification payloads are excluded from both fixtures.
A/B/A/B gives two boots per variant and three samples per boot for each CPU
count. Each directory sample lists 32 files 128 times; metadata samples add a
path stat per entry. These are existing ordinary C paths, not a before/after
comparison with the previously unsupported Rust read_dir. Launch controls use
1,024 empty echo children after warmup. Host Remote wall milliseconds follow,
median (minimum–maximum), six samples per cell.

| CPUs | Control | Baseline | Candidate |
| --- | --- | --- | --- |
| 1 | RAM list | 14.34 (13.74–15.24) | 14.45 (14.04–15.16) |
| 1 | RAM list + stat | 25.24 (24.81–26.04) | 25.62 (24.99–26.43) |
| 1 | npfs list | 84.19 (83.22–84.49) | 85.23 (84.36–87.82) |
| 1 | npfs list + stat | 185.15 (184.16–187.00) | 184.90 (182.30–186.27) |
| 1 | launch | 852.09 (841.66–858.82) | 854.09 (849.98–860.13) |
| 4 | RAM list | 11.22 (10.60–15.30) | 10.71 (9.92–14.22) |
| 4 | RAM list + stat | 24.05 (21.36–31.93) | 22.41 (21.36–30.41) |
| 4 | npfs list | 97.23 (80.87–134.95) | 113.67 (80.28–135.35) |
| 4 | npfs list + stat | 384.49 (257.26–392.90) | 347.65 (199.09–371.17) |
| 4 | launch | 903.75 (893.93–911.22) | 883.85 (874.64–912.36) |

VM/host vmstat is retained. Four-CPU npfs ranges overlap widely; guest steal
and occasional host swapping remain visible. No native speedup or regression
is inferred. Plain launch and ordinary one-CPU controls show no substantial
change in this series. Timing excludes compiler/SDK builds.

An extra open Remote session consumes 392 PMM pages (1,568 KiB) in every A/B
boot and returns to a two-page retained increment after close. Steady backing
varies by 64 pages when an additional kernel heap pool is retained; there is no
new consistent pool-sized increment. With the same pool count the candidate is
one page higher. Boot-reserved archive bytes are separate from this PMM count.
These observations describe the control fixture, not every Rust application's
allocation or the per-entry cost of retaining a directory.
