# Native filesystem task 5: QEMU end to end

Qualification on 2026-10-04, based on merged main `7955f59`, with pinned userland
`06812bc`, filesystem `d352c7e`, ports `bf7667c` and lwIP `a1aadb9`. Installer
PRs parent #369 and userland #109 were confirmed merged before this task.
The owner accepted finishing QEMU and deferring native ThinkPad installation
until writable USB storage is available. No storage driver or ABI changes belong
to this qualification.

## Build inputs

The kernel and host tools were built from current main. The unchanged SDK,
userland and ports products were reused explicitly from the task-4.3 build,
with their existing bundle manifests and payload verification. SDK provenance
records parent `119ad0e` modified, clean userland `06812bc` and filesystem
`d352c7e`; userland/ports use that exact SDK identity. Comparing `0b04ea8` to
current main showed no changes to public ABI/P1F headers, SDK export/codec rules,
shared shebang source or dependency pins. Reuse therefore preserved the matching
application inputs while rebuilding the changed kernel. It does not claim a
fresh SDK/application source build at `7955f59`.

```sh
make -j16 image usb-image PREBUILT="sdk userspace ports" \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  PYTHON=/usr/bin/python3 BOOT_MENU_TIMEOUT=5
```

Existing Lua 5.4 build tools were on PATH. GCC 16.2.0 and the existing Pyxis
binutils/compiler were used; no container rebuild was needed. Effective kernel
configuration retained the 30 s npfs flush interval, 120 HPET maintenance ticks
and disabled native xHCI. USB firmware boot does not need native xHCI I/O.
The ordinary ISO and 1 GiB USB image both built successfully. Existing main CI
run 782 passed build and filesystem jobs for `7955f59`.

## Manual installation

QEMU 10.2.2 under nested KVM used q35, four CPUs, 2 GiB and matching OVMF
code/variables from `/usr/share/edk2/ovmf`. The QEMU binary was the previously
qualified `/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64`; this run attached
no CD-ROM. Each run used a fresh writable variables copy. Installation attached:

```text
-drive if=none,id=live,format=raw,readonly=on,file=build/pyxis-usb.img
-device qemu-xhci,id=xhci
-device usb-storage,bus=xhci.0,port=1,drive=live,bootindex=1
-drive if=none,id=target,format=raw,cache=writeback,file=build/task5/target.raw
-device virtio-blk-pci,drive=target,disable-legacy=on
```

The target was a new blank 2 GiB regular raw image with 512-byte logical sectors.
VirtIO net used the ordinary user backend. No VirtIO RNG was attached: the log
reported CPU RDSEED with RDRAND fallback ready and its existing boot self-test
passed. This observes the CPU entropy path in nested KVM, not native firmware.

Select Install Pyxis manually in the five-second Limine menu, then Super+Right
for its terminal. Read the room admitted the blank target, displayed its size
and unavailable old GUID, prefilled a 12 MiB journal, and required typed `wipe`.
The installer reported `installed` after its flush/release/rescan, ESP traversal
and byte comparisons, and normal read-only pool/marker verification. The live
USB image's full SHA-256 stayed unchanged. Kernel GPT logs showed a healthy new
map with ESP LBA 2048/count 1048576 and pool LBA 1050624/count 3143640.

This mixed-device launch intentionally uses a raw QEMU command. The existing
`run-usb` launcher rejects a VirtIO disk so its native-USB qualification cannot
be masked by another backend; that contract remains unchanged. Here the live
medium is firmware-loaded and the install target is explicitly VirtIO. The run
does not establish native USB block access.

## Installed boot and persistence

The target booted by itself with fresh OVMF variables, no USB medium and no ISO.
Limine used timeout zero and no installer entry. The read-back configuration
selected `init-installed` and new GUID `8bfb16a7-116f-445b-a7d4-7e32a0e7ebb8`.
The fixed init reached the local `home://` prompt, configured net0 and mounted
writable `system://`; CPU entropy remained available without VirtIO RNG.

Ordinary shell commands copied existing boot data and an existing native program
to the persistent volume, then synchronized it:

```text
cat app://share/hello.txt > system://hello.txt
cat app://cat.pxe > system://cat.pxe
sync system://
```

After stopping QEMU, the extracted pool passed `fsck.npfs`. Its listing contained
regular `SAFE_TO_WIPE`, `hello.txt` (661 bytes) and `cat.pxe` (51,576 bytes).
Both extracted files matched their archive sources with `cmp`. A second
target-only boot used fresh OVMF variables again. Running
`system://cat.pxe system://hello.txt` printed the complete known text and returned
to the prompt, demonstrating persisted executable capture and file reads after
reboot. The stopped pool then passed structural checking again.

## Host inspection and limits

The installed GPT passed `sgdisk --verify`, with only its advisory that the
pool's 4 KiB aligned end is not a 2048-sector boundary. The extracted ESP passed
`fsck.fat -n`; the installed and synchronized pool images passed structural
checking. Extracted EFI, kernel and archive matched their original sources with
`cmp`. These are ordinary success/consistency observations, not crash or
uncertain-I/O qualification. Broader 4 KiB-sector, multi-pool veto, cancellation
and reinstall cases remain in the unchanged
[task-4.3 record](../native-filesystem-task4.3/README.md).

Native ThinkPad installation is deferred by owner choice. Current Caelum has
writable VirtIO storage but neither USB mass-storage reads/writes/flush nor NVMe.
Firmware USB boot and descriptor enumeration cannot substitute for a writable
native target. Resume physical validation after the USB read/block-integration
and real write/cache-synchronization work in the
[USB milestone](../../../devices/usb-installation.md). Select an expendable target
explicitly then; the internal Fedora disk has not been touched. No physical-media
or power-loss claim follows from these QEMU runs.

The original qualification changed documentation only and made no matched
performance claim. No tests, fault injection, CI,
validation programs or boot/output automation were added. Interaction used
manual monitor keystrokes/screenshots and ordinary shell/host tools. All QEMU
and debugger jobs were stopped. The original checkout and task-3 experiment
records were preserved.

## Installer review follow-ups

Parent #371 additionally pins userland `d730e4f` from
[userland #111](https://git.internal/PyxisOS/pyxis-userland/pulls/111), addressing
the blank-disk wording and template note on #371 and partial-failure guidance
left from #369. Consent eligibility and veto rules are unchanged. The recovery
hint requires booting the live image again before choosing Read the room; source
review checked its attempted-write guard and reboot requirement. No write
failure was induced, so this is not runtime failure/recovery qualification.

Full source image and USB builds passed with the same compiler, Lua tools and
kernel configuration. A temporary local template line `default_entry: 2` selected
Install Pyxis for a manual QEMU run (q35, nested KVM, four CPUs, 2 GiB, no VirtIO
RNG). Read-only USB live media and two 2 GiB, 512-byte-sector VirtIO targets were
attached. The zeroed target was listed as `blank disk: no partition table`; the
other had nonzero unpartitioned contents and kept the unrecognized-layout reason.
Installing the blank target with the 12 MiB default succeeded. The other target's
whole-disk SHA-256 remained unchanged. Stopped-target GPT, FAT and npfs checks
passed, with the same GPT end-alignment advisory as above.

Read-back installed configuration had timeout zero, the new GUID and
`init-installed`, with neither `default_entry` nor the Install entry. A target-only
boot with fresh OVMF variables reached the ordinary local prompt. The temporary
template edit was reverted; ordinary timeout-zero image assembly was restored.
These follow-ups do not replace the earlier persistence measurements or the
deferred physical validation. All QEMU jobs were stopped.
