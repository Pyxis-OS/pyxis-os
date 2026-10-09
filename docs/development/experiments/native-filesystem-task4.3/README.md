# Native filesystem task 4.3 validation

Manual validation on 2026-10-04 for `fs/native-installer`, based on parent `88aa838` (including the installed-menu decision),
with published userland `4d133b0`, filesystem `d352c7e`, ports `bf7667c` and lwIP `a1aadb9`. The parent SDK and image changes were
local during these runs (the SDK manifest records that modified state and clean dependency revisions), and no filesystem or ports
changes were needed. The [installer reference](../../../userland/installer.md) records the implemented contract.

## Build and environment

Ordinary full source image and host-tool builds passed with the existing GCC 16.2.0/binutils toolchain, host GCC 16.2.1, Python 3
and the Lua/Kconfig tools; the final runtime image used `make -j16 image fs-tools PYTHON=/usr/bin/python3 BOOT_MENU_TIMEOUT=5`. The
final archive was 21,152,256 bytes, the ISO 27,971,584 bytes and the installer PXE 103,719 bytes. The SDK contains the target npfs
codecs, header and license and records the filesystem revision and state, and the guest SDK also packages the archive. No compiler
rebuild, tests, fault injection, CI or automation were added.

Runtime used QEMU 10.2.2 under nested KVM: q35, four CPUs, 2 GiB, modern VirtIO block devices with writeback cache, VirtIO RNG and
network, and OVMF with a fresh variables copy per installation and target-only boot. Install Pyxis was selected manually in the
five-second menu, then Super+Right for the installer terminal; monitor `stop`/`cont`, `sendkey` and `screendump` supported manual
inspection, and GDB was read-only. Fixtures were disposable regular image files, never host block devices.

## Observations

- An initial marked 2 GiB, 512-byte-sector target and a separate blank disk were listed correctly. Normal installation selected the
  sole eligible marked target, displayed its GUID and old-system label, prefilled a 12 MiB journal and required typed `wipe`. The
  program reported `installed` after flush, release/rescan, FAT traversal and source comparisons and normal read-only pool
  reopening and marker lookup, and the nonselected blank image hash stayed unchanged.
- Target-only UEFI boot with no live ISO selected the fixed `init-installed` without a menu, configured net0 and reached the
  RAM-home prompt; `ls system://` showed `SAFE_TO_WIPE`. An early screenshot taken before completion looked stalled, but the
  eventual result and idle worker inspection showed no filesystem stall.
- Host FAT checking first found a BPB volume label without a matching root label, so the installer now writes `NO NAME`. The final
  reviewed build was reinstalled on the marked target with the journal edited from 12 to 16 MiB: success, with `fsck.fat -n` and
  `fsck.npfs` passing and `sgdisk --verify` finding no problems (advisory only: the pool's 4 KiB aligned end is not a 2048-sector
  boundary). Extracted EFI, kernel and archive files matched their sources with `cmp`.
- Four disks exercised selection and cancellation: two eligible marked targets, one blank 4 KiB-sector disk, and one disk holding two
  separately formatted npfs pools (a disposable one with a marker, a final one without). Normal mode required a disk number, excluded
  the blank and final-pool disks and refused selecting the latter; selecting a marked target and typing `no` instead of `wipe` exited
  unsuccessfully with “Nothing was written.” All four full-image SHA-256 values matched their pre-run values.
- Read the room admitted the blank disk while still excluding the two-pool disk (the marked pool did not override the final pool).
  Selecting the blank 4 KiB target and accepting the 12 MiB default completed installation, and every nonselected image kept its
  hash. For the stopped 4 KiB image, independent host decoding showed matching computed and stored primary, backup and array CRCs
  and identical GPT arrays; ESP LBAs were 256–131327 and pool LBAs 131328–524282; the extracted FAT and npfs images passed their
  checkers, EFI, kernel and archive matched their sources, and the installed template had timeout zero, the new GUID,
  `init-installed` and no installer entry.
- The 4 KiB target booted alone from fresh variables, `mkdir system://check` and `sync system://` succeeded, and a second target-only
  boot listed `SAFE_TO_WIPE` and `check/` with the stopped pool passing structural checking.

## Review and limits

Bounded source reviews covered formatting and FAT verification, the consent journal overlay, root-marker paths, GPT geometry, resource
ownership and SDK integration. One finding was a wrong-span header candidate compared before per-copy geometry validation, which could
bypass final-pool classification under Read the room; `4d133b0` validates each candidate first, matching kernel fallback (resolution
recorded on userland PR109), and earlier SDK rebuild-dependency and terminal error-buffer findings were corrected. Runtime fixtures
had EMPTY journals: full COMMITTED-log validation and overlay, damaged-copy fallback and uncertain failures were source-reviewed, not
exercised with corruption, fault injection or power loss. Consent is a bounded root-marker inspection, not a whole-filesystem ownership
proof, and physical firmware, USB/NVMe and ThinkPad end-to-end installation remain unqualified. No native installer existed for matched
install timings, so these runs establish function, not performance (earlier task-3/4.2 measurements stay in their records); revisit
installer inspection costs with ordinary large-log workloads when needed.

## Final integration and geometry refinement

Main's RTL8111/network-profile merge `eaeb417` was incorporated in parent `119ad0e`; a full source build passed with clean parent and
userland `4d133b0` inputs, the 4 KiB target was reinstalled normally with the 12 MiB default and booted alone to the local session
with net0 configured, kernel storage code was unchanged, and existing parent CI run 774 passed build and filesystem for `119ad0e`.
Documentation review then found an overstatement of supported sector sizes: kernel GPT rescan supports only 512 and 4096 bytes, and
published userland `06812bc` aligns installer admission with that boundary before raw opening (a full image rebuild passed, and the
512/4096 write paths are unchanged). A manual Read-the-room boot with one marked 4 KiB disk and blank 1024/2048-byte-sector candidates
excluded both of the latter because the VirtIO driver rejected their setup, so this run did not directly reach the new geometry guard,
which source review confirms occurs before raw access; the 4 KiB target stayed eligible, Ctrl+C cancelled the prefilled journal prompt
and all three full-image hashes were unchanged. The finding and resolution are recorded on parent PR369 with dependency PR109 linked;
final parent updates change only the published userland pin and documentation, and userland has no standalone Actions tasks.
