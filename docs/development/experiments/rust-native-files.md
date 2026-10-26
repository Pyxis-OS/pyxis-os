# Rust native writable files

2026-10-26, the writable-file slice of the [std milestone](../../wip/rust-std.md).
Directory enumeration/mutation remains separate; no native/PXE run is involved.

## Inputs and build

Baseline kernel/SDK source `742817b0`, userland `4060486d`, ports `4f2e8aeb`,
fs `f3f22b6e`, lwIP `d52ebd20`. The baseline precedes implementation. Selected
native programs rebuild against that SDK. Candidate userland is `9acda3e4`,
libc crate `94498eae`, Rust fork `7bb492df`; both cost fixtures use identical
baseline kernel bytes and the same root-file inventory, devices and config.
The candidate's SDK/native programs and Rust libraries rebuild together.

Rust nightly-2026-10-24 base `48f4b055`, matching CI LLVM 23.1.4, final Pyxis
Clang/LLD 23.1.3 (`3d69e83`). The clean published fork rebuilds compiler and
host/target libraries offline on horse, with full bootstrap, LTO and embedded
bitcode off. Exported compiler identity matches the source and receipt. All
26 target rlibs / 506 ELF members lack `.llvmbc` and `.llvmcmd`; native metadata
and authority constants match the exported SDK. No compiler-container rebuild,
kernel operation, ABI layout change or new grant.

## Functional qualification

Nested KVM/q35, host CPU, one/four vCPUs, 2 GiB, OVMF, VirtIO GPU/net/RNG,
read-only SCSI ISO; loopback Remote/GDB. Disposable RAM and RAM-npfs roots,
plus a confined host directory through virtiofsd. All following checks pass:

- Create, exclusive-create preservation, read/write/truncate; seek and shared
  clone cursor; growth zero-fill, shrink/regrowth, overflow refusal and sync.
- Real short native writes and write_all continuation; vectored fallback uses
  the first nonempty slice. Empty vectors preserve position.
- READ-held File versus writable path; locally changed Permissions grants no
  authority. Applied permissions remain Unsupported. Directory WRITE_FILES
  alone is readonly; CREATE-only and REMOVE-only are writable.
- Held files survive rename, replacement and removal using existing C calls
  for namespace setup. Rust directory mutation is not claimed.
- Raw path order, missing intermediate components, captured relative cwd,
  held resources across cwd changes, NUL rejection, readonly/missing roots and
  immutable TEXT files. Append and invalid options preserve existing/absent names.
- Provider writes and unknown-authority path metadata refuse before preparation.
  Scoped GDB sees zero provider OPEN/STAGE calls through those refusals, then
  exactly one READ OPEN and zero STAGE calls for the successful HTTP read.
- C-helper failure outputs stay unchanged; held PIPE/CONSOLE metadata remains
  available with protocol-specific rights. Existing C append remains supported.
- Rebuilt std main, managed workers/TLS/contended sync, core+alloc hello and
  tiny-skia (`d0dfb9f8f80d6c59`) pass. Rebuilt hexyl ASCII/no-colour output matches
  the pinned upstream Linux build byte-for-byte.

After rebasing onto main `e13b8f35`, the rebuilt integration kernel `d8cc8910`
repeats the one/four-CPU functional set successfully, including main's caller-CPU
MEMORY transaction changes. The final userland head `e80f50f8` rebases the same helper/runtime blobs onto
package/update main `e2c0ab4d`; rebuilt libc/libpyxis archives are byte-identical
to the measured candidate. SDK/native programs rebuild and the one/four-CPU
functional set repeats after that restack. Main `4108405e` adds no kernel-code
change relative to `e13b8f35`. The matched cost fixtures retain the original
baseline kernel to isolate this task.

The existing std sample's former blanket writable-Unsupported expectation is
updated to PermissionDenied for its read-only bundle license. Raw evidence,
build/fixture hashes and private workload commands remain under
`/shared/present/rust-native-files/`; no test framework is added to the tree.

## Matched costs and backing

After builds, repeat A/B/A/B for each CPU count: two boots per variant,
three samples per boot. Each file sample performs 1,024 path metadata/open/
held metadata/read/close iterations. Launch samples use Lua's existing native
runner for 1,024 empty echo children after warmup. Values are host Remote wall
milliseconds, median (min–max), six samples per cell.

| CPUs | Workload | Baseline | Candidate |
| --- | --- | --- | --- |
| 1 | archive | 20.6 (19.2–31.5) | 23.1 (22.3–24.1) |
| 1 | npfs | 42.9 (40.6–48.3) | 46.2 (43.7–51.1) |
| 1 | launch | 894.0 (856.0–1135.6) | 1129.3 (915.8–1676.4) |
| 4 | archive | 26.7 (20.8–35.3) | 23.0 (17.2–48.7) |
| 4 | npfs | 215.8 (48.6–312.1) | 120.5 (74.8–182.7) |
| 4 | launch | 1152.8 (1027.7–1363.5) | 946.4 (917.0–1035.3) |

Nested timing is noisy: one-CPU launch median rises while four-CPU falls, with
wide overlapping ranges. Host/VM scheduling, swap and steal are visible in
vmstat, so these data do not establish a source-level speedup or regression.
The initial baseline and additional brackets are retained; an earlier series
that overlapped a compiler build is excluded, as is a preliminary fixture with
inactive extra files. There is no native performance claim.

The candidate cost archive grows 36,352 bytes (44,409,344 → 44,445,696), covering
the selected native helper code and updated read workload. PMM steady allocated
backing is normally two pages higher; one four-CPU candidate boot additionally
retains one 256 KiB kernel heap pool. An extra open Remote session consistently
costs 391 baseline versus 392 candidate pages: 1,564 versus 1,568 KiB.
Both return to the same two-page retained increment after close. Boot-reserved
archive backing is separate from PMM's allocated-page counter. These are fixture
backing observations, not a claim that every Rust application has this footprint.

Append, provider writers, directory iteration/mutation, permission application,
locks and unreported timestamps remain explicit Unsupported boundaries. File
flush is unbuffered; sync is the backend's real operation, and checked-close
failure retains the existing process-termination rule.
