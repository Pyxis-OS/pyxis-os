# Hexyl on native Rust std

2026-10-25, the read-only consumer step of the [std milestone](../../wip/rust-std.md).
The initial qualification preceded the 0.0.9 tag; the post-release recheck is
recorded below.
This qualifies hexyl, not managed Rust workers or the full filesystem profile.

## Inputs and build

Hexyl v0.17.0 is commit `8eb6d4771ce1ec7af65d06bd335457783b77d557`;
its archive hash and cache URL are in the milestone and port recipe. Ports
`76f154a6` contains three ordered patches: standard NotSeekable handling,
read-error propagation/final output flushing, and native terminal_size.
The registry graph stays locked; target vendoring/build uses only the closed
Cargo proxy, then frozen/offline compilation. Upstream BrokenPipe success
remains intentional. Complete crate/table notices, EPL-1.0, source, locks,
patches and SDK/Rust receipts accompany the package.

Rust `a5c9a1815816cddb77f5c58238adf549e618b3a4` supplies the own-built
Linux-host compiler and both host/Pyxis libraries; libc crate `417acae0` adds
truthful SDK file bindings. Rust LLVM is 23.1.4; final Pyxis Clang/LLD is 23.1.3.
Panic aborts; LTO and embedded bitcode are off. The separate prefix exports the
canonical SDK linker adapter and hashes compiler/Cargo, libraries and metadata.
No user RUSTC_BOOTSTRAP, compiler-container rebuild or Rust dependency in normal
image CI is added. The full compiler/library build and fresh export pass.

The unchanged gate-2 SDK is source `1230d8da`, userland `14be162e`, fs `f3f22b6e`;
its manifest SHA-256 is
`8e157596c9c9eb849002b8802f7d899a0d14f0fdda4214c07ba9a224e4e86946`.
Kernel/header/runtime inputs are unchanged; no native C helper or kernel ABI
was added for this consumer. The existing [native-program controls](rust-std-main.md#matched-native-program-controls)
remain the runtime baseline, without claiming new hexyl performance numbers.
The actual recipe and package checks were run separately using that SDK:

```sh
lua ports/build.lua hexyl --sdk SDK --rust PREFIX --work FRESH_WORK
python3 tools/pxpkg.py pack --build build packages/hexyl.lua REPOSITORY
python3 tools/pxpkg.py publish --repository qual REPOSITORY
python3 tools/pxpkg.py check REPOSITORY
```

This did not run the aggregate `make hexyl-package` or rebuild the unchanged SDK.
The optional Make target invokes those existing paths for ordinary use.
Package `hexyl@0.17.0+a4ed70d70a2d` contains 60 files, 27,651,584 archive bytes;
its executable is 827,948 bytes. It was served only through a loopback test
repository, explicitly configured unsigned using the existing development policy.
No public repository was signed or published.

## QEMU and debugger results

Nested QEMU 10.2.2/KVM q35, host CPU, one/four vCPUs, 2 GiB, OVMF, modern
VirtIO GPU/net/RNG and SCSI read-only ISO. There is no disk or native access.
The same fixture uses RAM home and a 512 MiB RAM-npfs backing budget for package,
staging and system volumes. All launch, staging, package and root grants are
explicit. Fixture identity is `a4ed70d70a2d`, ABI 0.0.8; frozen kernel SHA-256 is
`9a83ebd757ee37088e75bc743ecdccfbf49944f9ffe0fbb6e451532f4660b50d`.
This is a development qualification image, not a released 0.0.9 artifact.

An unmodified Linux build of the same upstream source is the independent output
reference. Deterministic comparisons use `--character-table=ascii --panels=2`
and explicit colour policy, clearing the host's inherited NO_COLOR where needed.
Both CPU counts pass:

| Exercise | Result |
| --- | --- |
| Empty, 33-byte tail, all byte values, repeated rows, 4 KiB binary | Exact reference bytes, exit 0 |
| Skip 17/count 67, end-relative skip -19, include mode | Exact reference bytes, exit 0 |
| Stdin pipe and forward-only skip | Exact reference bytes, exit 0 |
| Missing file/root, directory input, invalid/backward stdin seeks | Diagnostic and exit 1 |
| Missing stdin/stdout, including include-mode buffered output | Native error and exit 1; retired jobs collected |
| Always/Auto/Force, NO_COLOR, RGB, file redirection | Correct bytes/policy; Auto suppresses file colour |
| Regular RAM and installed RAM-npfs package file reads | Correct bytes, exit 0 |
| File metadata/Debug, shared cloned cursor, overflow, raw lookup order | Exit 0; write-open Unsupported before effects |
| Pipe File length | NotSeekable, followed by correct read/EOF |
| Updated ordinary std main and initial TLS cleanup | Exit 0 and destructor marker |
| Package check/install/verify and bare-name launch | New active set, verified files/indexes, exit 0 |

The stream-error driver uses existing native job launch with deliberately omitted
streams, not injected errors or a kernel test hook. It observes real process
results and closes retired jobs. Sources/captures stay outside the repositories.
A preliminary fixture with an older revision correctly refused the package;
the final matched image/repository fixes that identity mismatch rather than
bypassing admission.

GDB on both CPU counts reaches hexyl's ordinary C-main entry, native read-only
File open/read, FS self-pointer `0x4030` and 16-byte stack alignment. The same
libc-owned FS persists through file operations; no second TLS owner is installed.
The local console renders coloured ASCII panels and UTF-8 borders; automatic
width uses four panels on its supplied 160-column console, versus two at the
Remote console's 80 columns. An earlier incomplete local frame was retained
unedited under `screenshots-keep/glitches`; later and final captures render
correctly. Its cause is not established by this task.

## Artifact checks and limits

All 114 prefix receipt hashes match, including all 26 separate target metadata
files. The 46 target rlibs contain 566 x86-64 ELF members without embedded LLVM
bitcode. The final static ELF has no dynamic section, remaining relocations or
undefined symbols; link-map inputs stay in the selected target build, prefix
and SDK. All three P1F segment descriptors/payloads match ELF LOADs exactly.
TLS is 16 initialized/48 total bytes, alignment 8, with GOT outside TLS and no
dynamic TLS resolver. Final P1F SHA-256:
`3ee9dc962d72915700b57bb3cd1b475690183e1d462f64590b54a6b1f575e55f`.
This proves inspected artifact consistency, not reproducible-build attestation.

Read-only File ownership, conditional metadata and supplied streams are the
bounded admission. Writable opens, directory enumeration/mutation, permissions,
symlinks, file locking and Rust workers remain undelivered; network/process
creation remain explicitly unsupported. Native hardware, HOST/disposable-disk
filesystem qualification, every default glyph and performance were not run here.
No complete-std or general-crate compatibility claim follows from hexyl.

Raw commands, compiler/SDK receipts, images, host reference output, QEMU/GDB
captures and package records stay in `/shared/present/rust-hexyl/`.
The unedited final console screenshot is retained under `screenshots-keep/rust`.

## Post-release rebase, 2026-10-26

Gate 2 rebases onto main `09a19c94` as `17171e74`; hexyl restacks on that
head as `d0b05009`. Dependency bases were already current: userland `81ee85f9`,
ports `4d07f4c1`, Rust `pyxis` at `0ea5d9dd`, libc `libc-0.2` at `ccc2ff4a`.
Their published heads and selected compiler prefixes therefore remain unchanged.
Child PR bases now name their parent branches, excluding duplicated parent diffs.
After each parent merges, retarget its child to the integration branch before
merging it; this does not require replaying or changing its commits.

The existing compiler container rebuilds the SDK and current-main kernel.
SDK headers, startup, linker script and libraries are byte-identical to the
initial qualification; only its manifest and two pkg-config paths differ.
The actual ports recipe and both ordinary-main samples rebuild/relink against
that SDK. Fresh one/four-CPU guests repeat every hexyl case above, plus gate 2's
18 main/args/TEXT/TLS, allocation, clock/entropy, grant/failure, overflow,
file/pipe, late-C++-TLS and core+alloc regression cases: all pass. GDB repeats
main/FS/alignment and native File open/read on both CPU counts. Final artifact
checks pass again; executable SHA-256 is
`3057b0d73236cdc42893853b259f887ad43a4afe9f86d9fef929e6a09d7ff8b3`.
The private ABI-0.0.9 package `0.17.0+d0b05009f748` installs and verifies on
both guests. No new performance or native results are claimed. Rebase evidence
is retained in `/shared/present/rust-rebase-20261026/`.
