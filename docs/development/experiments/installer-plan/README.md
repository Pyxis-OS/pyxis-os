# Installer planner/executor — QEMU, 2026-10-11

Baseline captured before code changes; implementation and manual qualification use matching targets and devices.

Baseline source: parent `b8b0b7d82337` (accepted documentation on main
`a59a4ef3`), userland `d2437266d459`, ports `df3210b04142`, fs `b427df29f865`.
Full SDK/image/fs-tools build used the existing LLVM49 container; no compiler
container rebuild. The revision in the baseline image is `b8b0b7d82337`.

Linux `6.19.10-300.fc44.x86_64`, QEMU 10.2.2, q35 nested KVM, CPU `max`,
four vCPUs, 2 GiB, HPET, UTC RTC, fresh OVMF variables. VirtIO block: 2 GiB
raw target, 512-byte sectors, writeback, one queue; VirtIO entropy; no NIC or
HDA. No task-owned build ran during timing. Shared-host load is uncontrolled.

Install used a fresh blank target, Read the room and the default 12 MiB journal.
Update restored the previous task's pristine installation at `94d0320d4134`;
it staged a different revision, avoiding same-revision clearing costs.
Hardware breakpoints with function calls disabled recorded source preparation,
inventory completion, the first exclusive claim boundary and restart offer.
Host monotonic timestamps and `/proc/<qemu-pid>/stat` record whole-QEMU CPU,
including device emulation; this is not guest BSP or native stick cost.

| Flow | Samples | Inspection wall s | Apply wall s | Whole-QEMU apply CPU s |
| --- | --- | --- | --- | --- |
| Baseline Install | 1 | 0.696 | 153.871 | 127.71 |
| Baseline Update | 1 | 0.595 | 141.955 | 115.44 |
| Implementation Install | 1 | 0.424 | 159.611 | 134.54 |
| Implementation Update, legacy scan | 1 | 52.114 | 179.711 | 169.34 |

Baseline inspection ends at target selection. Implementation preparation starts
with target reinspection after immutable source inventory and ends when the plan
is ready; these inspection columns have different boundaries. Apply consistently
runs from confirmed executor entry/first claim to restart offer, excluding
manual Review/journal/consent time. Both runs copied and compared 2,211 regular files /
42,053,098 bytes, then passed EFI 5/5 raw path/payload verification. Program
verification remains ordinary filesystem-view reads and may use cached pages.
No physical-media or power-loss result is claimed. Raw images/logs remain local.

Build: `make -j16 image fs-tools BOOT_MENU_TIMEOUT=60` in the existing builder.
For interactive reproduction, use a fresh blank or restored raw target:

```sh
make run CPUS=4 MEMORY=2G ACCEL=kvm BOOT_MENU_TIMEOUT=60 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VIRTIO_BLK_IMAGE=build/target.raw VIRTIO_NET=0 VIRTIO_RNG=1
```

Measurements used the equivalent explicit q35 launch, `-cpu max -rtc base=utc`,
512-byte logical/physical block sizes, fresh writable OVMF variables and
`-gdb tcp:127.0.0.1:1237`, plus local serial/QMP sockets for manual inspection.
Select Install Pyxis, then Install / Read the room or Update.

Implementation measurements use parent/kernel `b8b0b7d82337`, userland
`8d86ca4`, and the same ports/fs pins as baseline. Selected program payloads
remain 2,211 files / 42,053,098 bytes. Legacy Update compares the old revision's
2,321 entries / 42,046,106 bytes, and publishes a 390,998-byte new manifest.
The legacy scan is explicit at preparation and repeated under exclusive ownership.
A valid old manifest avoids that full payload scan. These are single samples,
not a statistical regression estimate; shared-host load limits attribution of
wall-time differences.

| Implementation legacy Update phase | Wall s | Whole-QEMU CPU s |
| --- | --- | --- |
| Exclusive revalidation through program-copy start | 51.853 | 63.03 |
| Copy | 85.981 | 57.52 |
| Pool sync | 1.202 | 0.77 |
| Filesystem-view/source-byte/SHA verification | 16.804 | 20.15 |
| Manifest publication/sync/verification and handle close | 0.177 | 0.20 |
| EFI replacement and flush/release | 11.875 | 13.89 |
| Raw EFI verification, system reopen and cleanup | 11.819 | 13.78 |

Hardware breakpoints observe phase boundaries; no target function calls, register
writes or memory writes. One debugger-only attempt crashed GDB while changing
Python breakpoint objects; it is excluded from apply measurements. Plain temporary
breakpoints captured the qualified apply. Earlier replay transcription mistakes
were refused without writes; read-only debugger inspection established the actual
hashes before positive replay. This was an input error, with no code change.

## Manual qualification

Install and replayed legacy Update both reached success after program and manifest
verification plus EFI 5/5 raw path/payload verification. The updated target booted
its new revision and `cat home://kept.txt` returned `progress-kept`; a native
`poweroff` checkpointed the pool before subsequent read-only checks.

Strict malformed/duplicate-field handling, duplicate target identities,
unidentified-target serialized refusal, unknown prior history, and removed-root
classification are inspected code paths, distinct from the interactive results.
No new harness or test program is added. Raw disks, screenshots, debugger scripts
and logs stay in ignored local build files. No native-media/power-loss claim.

Replaying the pre-update record against the updated/checkpointed target refused
before writes: QEMU target write operations/bytes were zero, and the whole raw
disk SHA-256 was unchanged.

The final current-main integration build used parent `74943e454c4b`, userland
`e878f4f93baf` (rebased onto `95888f0`) and ports `28b5a3bce9aa`. Full
SDK/image/fs-tools passed with the existing compiler. On that image, Update
read the published manifest, reported updated/unchanged roots with old/new sizes
without a legacy-scan prompt, then Review's Dry-run exited successfully.
The target recorded zero write operations/bytes and retained its whole-disk hash.
This integration check is separate from the matched timing images above.

On the same rebuilt image, a blank-target Install reached Review, then the final
`wipe` prompt was cancelled with a different word. The executor refused before
claiming write access; QEMU target write operations/bytes were zero and the whole
raw-disk hash was unchanged. Printing had the same zero-write/hash result on the
legacy target.
