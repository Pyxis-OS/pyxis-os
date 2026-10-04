# Native filesystem task 4.3 validation

Manual validation on 2026-10-04 for `fs/native-installer`, based on parent
`88aa838` (including the installed-menu decision), with published userland
`4d133b0`, filesystem `d352c7e`, ports `bf7667c` and lwIP `a1aadb9`.
Parent SDK/image changes were local during these runs; the SDK manifest records
that modified state and clean dependency revisions. No filesystem or ports
changes were needed. The [installer reference](../../../userland/installer.md)
records the implemented contract.

## Build and environment

Ordinary full source image and host-tool builds passed with the existing GCC
16.2.0/binutils toolchain, host GCC 16.2.1, Python 3 and existing Lua/Kconfig
build tools. The final runtime image was built with:

```sh
make -j16 image fs-tools \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  PYTHON=/usr/bin/python3 BOOT_MENU_TIMEOUT=5
```

The build host's existing Lua 5.4 runner was on PATH. The final archive was
21,152,256 bytes, ISO 27,971,584 bytes and installer PXE 103,719 bytes.
The SDK contains target npfs codecs/header/license and records the filesystem
revision/state; the guest SDK also packages the archive. Compiler rebuilding,
new tests, fault injection, CI or boot/output automation were not added.

Runtime used QEMU 10.2.2 under nested KVM, q35, four CPUs, 2 GiB, modern
VirtIO block devices with writeback cache, VirtIO RNG/net and matched OVMF
code/variables from `/usr/share/edk2/ovmf`. Each installation and target-only
boot used a fresh variables copy. Select Install Pyxis manually in the
five-second menu, then Super+Right for the installer terminal. Monitor
`stop`/`cont`, `sendkey` and `screendump` supported manual inspection; GDB was
used read-only to inspect worker/queue state. All QEMU/debugger jobs were stopped.
Fixtures were regular disposable image files, never host block devices.

## Observations

- An initial marked 2 GiB, 512-byte-sector target and separate blank disk were
  listed correctly. Normal installation selected the sole eligible marked
  target, displayed its GUID and old-system label, prefilled a 12 MiB journal
  and required typed `wipe`. The program reported `installed` after flush,
  release/rescan, FAT traversal/source comparisons and normal read-only pool
  reopening/marker lookup. The nonselected blank image hash stayed unchanged.
- Target-only UEFI boot, with no live ISO, selected fixed `init-installed`
  without a menu, configured net0 and reached the RAM-home prompt.
  `ls system://` showed `SAFE_TO_WIPE`. An early screenshot taken before
  completion looked stalled; the eventual result and idle worker inspection
  showed no filesystem stall.
- Initial host FAT checking found a BPB volume label without a matching root
  label. The installer now writes `NO NAME`; the final reviewed build was
  reinstalled on the existing marked target, editing the prefilled journal
  from 12 to 16 MiB. It reported success; `fsck.fat -n` and `fsck.npfs` both
  passed. `sgdisk --verify` found no problems, with its advisory that the
  pool's 4 KiB aligned end is not a 2048-sector boundary. Extracted EFI,
  kernel and archive files matched the source files with `cmp`.
- Four disks exercised selection and cancellation: two eligible marked
  targets, one blank 4 KiB-sector disk, and one disk containing two separately
  formatted npfs pools (disposable with a marker, final without one).
  Normal mode required a disk number, excluded blank and final-pool disks and
  refused selecting the latter. Selecting a marked target and typing `no`
  instead of `wipe` exited unsuccessfully with “Nothing was written.” All
  four full-image SHA-256 values matched their pre-run values.
- Read the room admitted the blank disk while still excluding/refusing the
  two-pool disk. The marked pool did not override the final pool. Selecting
  the blank 4 KiB target and accepting the 12 MiB default completed installation.
  Every nonselected image retained its pre-run SHA-256.
- For the stopped 4 KiB image, independent host decoding showed matching
  computed/stored primary, backup and array CRCs and identical GPT arrays.
  ESP LBAs were 256–131327, pool LBAs 131328–524282. Extracted FAT and npfs
  images passed their host checkers; EFI, kernel and archive matched sources.
  The installed template had timeout zero, the new disk GUID, `init-installed`
  and no installer entry.
- The 4 KiB target booted alone from fresh OVMF variables. `mkdir system://check`
  and `sync system://` succeeded. A second target-only boot listed both
  `SAFE_TO_WIPE` and `check/`; the stopped pool passed structural checking.

## Review and limits

Independent bounded source reviews covered formatting/FAT verification,
consent journal overlay, root-marker paths, GPT geometry, resource ownership
and SDK integration. Review found a wrong-span header candidate being compared
before per-copy geometry validation, which could bypass final-pool classification
under Read the room. `4d133b0` validates each candidate first, matching kernel
fallback, and its resolution was recorded directly on userland PR109. Earlier
SDK rebuild-dependency and terminal error-buffer findings were also corrected.

Runtime fixtures had EMPTY journals. Full COMMITTED-log validation/overlay,
damaged-copy fallback and uncertain failures were source-reviewed, not exercised
with manufactured corruption, fault injection or power loss. Consent is a
bounded root-marker inspection, not a whole-filesystem ownership proof.
Physical firmware, USB/NVMe and ThinkPad end-to-end installation remain unqualified.
Task 5 remains open.

A pre-implementation ordinary build passed, but no prior native installer exists
for matched install timings. No new matched latency/traffic series was captured
in task 4.3; these success runs establish function, not performance. The kernel
storage path is unchanged by this task; existing task-3/4.2 measurements remain
in their original records and are not presented as new measurements. Revisit
installer inspection costs with ordinary large-log workloads when needed.
