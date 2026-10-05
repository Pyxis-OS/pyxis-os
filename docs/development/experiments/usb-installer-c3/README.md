# USB installer authority qualification

Manual QEMU qualification on 2026-10-05 for C.3 of the
[USB milestone](../../../wip/usb-installation.md). Physical installation is the
separately assigned C.4 task.

## Sources and scope

Baseline main was `102432b`, with fs `d352c7e`, ports `36d952e`, userland
`a3eb8d2` and lwIP `a1aadb9`. Pins are unchanged. Implemented raw authority
sources were `33a0aee`; `91346a8` additionally fixes a shadowing warning in the
mount guard. The installed first image reports `33a0aee6bd4`; newer live media
reports `91346a8d80a8`.

Installer inventory now waits for boot disk discovery to seal, then exposes all
retained observed VirtIO/USB candidates. The owner accepted partial USB topology
coverage, including unsupported EHCI, while lost registry records and incomplete
VirtIO bookkeeping still refuse inventory. Unseen devices cannot be listed.
Existing eligibility, typed consent, per-device write qualification, raw claims,
bounded I/O, flush, release/rescan and mount exclusion are reused. No installer,
driver, ABI-layout or selection-policy replacement was needed.

## Builds and machine

Ordinary `make -j16 image fs-tools BOOT_MENU_TIMEOUT=60` built the baseline,
followed by `make -j16 image BOOT_MENU_TIMEOUT=600` for manual menu selection.
Changed enabled images used the same 600-second menu. An initial local-variable
shadowing warning was corrected; the final kernel build has no new warnings.
Existing vendored Quake warnings remain. The existing cross compiler was used;
no compiler-container rebuild, dependency pin or CI change belongs to this task.

QEMU 10.2.2 from `/tmp/pyxis-qemu-ahci-fix/build` used q35 nested KVM,
`-cpu max`, four vCPUs (one socket/four cores/one thread), 2 GiB RAM, UTC RTC,
`/usr/share/OVMF/OVMF_CODE.fd` and fresh private OVMF variables per measured
process. xHCI was enabled; npfs background flushing remained 30 seconds.
All targets were disposable regular raw files, with 512-byte logical sectors
and writeback cache. VirtIO RNG supplied entropy. Live media was a read-only
CD-ROM ISO; target-only boots had no ISO or VirtIO disk. No physical device or
host filesystem export was attached.

Manual Limine/terminal input, screenshots and GDB observations were used.
No tests, self-tests, hooks or boot/output automation were added. The explicit
interrupted-Update case uses the real writer and a killed disposable VM, as
required by C.3; it does not model physical power-loss durability.

## Matched candidate inspection

Three baseline boots and three changed-source boots used the same healthy
finalized older 2 GiB VirtIO fixture, a separate blank 2 GiB USB target,
`qemu-xhci,p2=1,p3=1` with root port 1, and unsupported EHCI. The existing
installer's Update choice inspected candidates and stopped at typed confirmation.
No update was confirmed. GDB observed VirtIO counters before candidate inspection
and at confirmation; each pair was 5 to 54 published/completed requests:

| Sources | VirtIO inspection requests (three samples) | Additional USB inspection |
| --- | --- | --- |
| `102432b` | 49, 49, 49 | USB candidate not exposed |
| `33a0aee` | 49, 49, 49 | 3 reads / 1536 bytes per inspection |

Each group had zero sample range. The changed installer listed USB as
`ineligible: blank disk: no installation` and retained the eligible VirtIO
installation. USB had zero writes and one synchronization from boot write
qualification, not candidate inspection. VirtIO image bytes remained unchanged.
Initial accidental normal boots and incomplete debugger/reset attempts were
excluded before collecting the three valid baseline samples. These are virtual
request counts, not latency or physical-device performance measurements.

## USB installation and target-only boot

The USB-only target was `/home/chronium/tmp/usb-c3/usb-install.raw`, initially a
blank sparse 2 GiB file. With live `33a0aee` media, Install / Read the room listed
it as the sole eligible observed target. The default journal was 12 MiB for
this chosen capacity. After typed `wipe`, the installer wrote the fresh ESP and
pool, flushed/released/rescanned, byte-verified boot files and reopened `system`
read-only. It reported `installed` and exited 0.

Observed layout was disk GUID `b4e834fe-4a7f-480a-8047-c1e440533037`, ESP entry 1
at LBA 2048 for 1048576 sectors, and pool entry 2 at 1050624 for 3143640 sectors.
These sizes and GUID are fixture observations, not USB-driver constraints.
Final BOT observations were READY, writable/flush true, no latched failure,
11657 writes / 47714816 bytes, and three synchronizations including boot
qualification. The verification pool was retained read-only with an EMPTY
journal (sequence 1).

A fresh target-only boot used `usb-storage,bootindex=1`, the same writable target
and no live media. Limine reached installed init; `fastfetch` showed Caelum
`33a0aee6bd4` and npfs `system://`. GDB confirmed the pool writable and healthy
USB capabilities. Ordinary local shell commands copied and synchronized data:

```text
cat app://share/hello.txt > system://kept.txt
sync system://kept.txt
rm system://SAFE_TO_WIPE
sync system://
sha256sum app://share/hello.txt system://kept.txt
installer
```

The two 661-byte files had SHA-256
`aa947fd83f8021231c34df87f225c5d5e2db615b5041af7b84a7800dfb09c86d`.
The installer launched from the ordinary session refused missing install-mode
authority and exited 1. GDB observed an EMPTY journal (sequence 9) and no
writeback error before stopping the VM. The finalized saved-file image was
retained independently as `installed-with-kept.raw`, with another separate
copy for interruption/recovery.

## USB Update and raw-claim refusal

Fresh live `91346a8` media listed installed `33a0aee6bd4` and the same disk GUID
as eligible. After typed `update`, normal exclusive revalidation, ESP replacement,
flush/release/rescan, boot-file byte verification and read-only system reopening
completed. The installer reported `updated` and exited 0. Final observations were
BOT READY, no latched failure, 11620 writes / 47574016 bytes and three
synchronizations including boot qualification. The retained pool remained EMPTY
at sequence 9.

After closing the VM, `cmp` against `installed-with-kept.raw` covered every byte
before the actual ESP start (1048576) and from its end (537919488) through EOF.
Both comparisons passed: GPT identities/copies, the whole pool and all outside
slack were unchanged. The extracted ESP revision was `91346a8d80a8` plus newline.
Host `fsck.npfs` passed without replay; extracted `kept.txt` matched the source
byte-for-byte and retained its earlier SHA-256.

Mounted exclusion was checked after the updater's actual read-only verification
had closed its root handle while retaining the pool. The installer still held
its original trusted `disks` grant; the raw-claim list was empty and the USB
device remained writable/flush-qualified. In its own CPU 1 userspace context
(CS `23h`), the existing `disks_open` wrapper was invoked for the same device with
READ_WRITE. It returned `CALL_BUSY`; writes remained 11620, synchronizations 3,
and no claim appeared. No kernel worker routine was called from an arbitrary
debugger context.

The manual invocation used the executable call instruction at `4012bfh`, with
a hardware stop at its return `4012c4h`, valid stack output storage and the
actual startup handle. Saved registers were restored before normal execution
continued and reported success. These are debugger addresses for this measured
ELF, not runtime contracts. An earlier ordinary GDB inferior-call attempt used
a nonexecutable stack trampoline and faulted the userspace task; that entire
run was discarded and the baseline restored before this successful run. Stack
execution was never enabled and no production hook or test program was added.

A fresh protected attachment of the updated disk was listed as
`ineligible: read-only device`; the installer reported no eligible disks and
exited 1 without writes. GDB observed known WP set, writable/flush false and
zero writes/synchronizations. The whole raw-file hash matched before and after.
Unknown optional capabilities and real transport-failure cases remain the C.1
source-reviewed/physical-qualification limits; they were not manufactured here.

## Interrupted ESP replacement

An independent copy of the finalized saved-file baseline used the same newer
live media and normal Update confirmation. The manual debugger stopped the real
writer before NPFS_RAW_WRITE at byte offset 1056768 for 4096 bytes. Two prior
writes had completed, totaling 8192 bytes; the sole synchronization was boot
qualification. Keeping the debugger attached and the VM paused, QEMU was killed
with SIGKILL before publication of that third request.

Host inspection found the primary and backup FAT boot sectors cleared by the
writer, and mtools rejected the ESP as non-DOS media. The outside-ESP comparisons
against the saved-file baseline still passed. No disk-control bytes were injected.
The interrupted image was retained separately before retry. An earlier batch
debugger attempt detached/resumed and was excluded; the measured prefix is the
later interactive, held stop.

Fresh live media recognized the same disk as eligible with damaged FAT32
geometry and reported `Boot files damaged or missing; Update will rebuild them.`
After typed `update`, the ordinary updater completed and exited 0. Final BOT
observations were READY, 11620 writes / 47574016 bytes and three synchronizations
including boot qualification; the retained pool had an EMPTY journal at sequence
9. Comparisons again passed for every byte outside the ESP. Host `fsck.npfs`
passed without replay, and extracted `kept.txt` matched the original source.

Fresh target-only boots of both the normally updated disk and the recovered disk
had no live media or VirtIO disk. Both reached installed init, reported Caelum
`91346a8d80a8` and npfs `system://`, and returned the original SHA-256 for
`system://kept.txt`. The normal updated boot additionally had its writable pool
confirmed in GDB. [The recovered boot screenshot](recovered-boot.png) records
the installed revision and preserved file hash.

## Delivery and limits

At the initial C.3 qualification revision, `.config` remained `CONFIG_XHCI=n`;
an ordinary `make -j16 image` with that default also passed. The owner subsequently
requested the enabled default; see the review follow-up below. All task QEMU/debugger processes were
closed. Only disposable images were written; physical installation, PXE live
boot, a real power-off and the owner's `0.0.1` tag remain C.4.

These results qualify the existing installer path for the observed QEMU USB
medium. They do not qualify physical write durability, optional-command failure
recovery, unseen topology or other logical-sector sizes. Partial topology was
intentional in every live qualification boot through unsupported EHCI. No new
topology, capacity, GUID or debugger address is part of the implementation.

## Review follow-up: durable ESP publication and enabled xHCI

The [review on #407](https://git.internal/PyxisOS/pyxis-os/pulls/407) found that
old readable configuration/revision clusters could classify a partly rewritten
kernel/archive as a recognized installation. The owner requested an immediate
fix and separately changed the xHCI default to enabled on 2026-10-05.

[Userland #124](https://git.internal/PyxisOS/pyxis-userland/pulls/124), revision
`a3ea1b0`, changes the shared ESP writer: clear and flush the reserved area before
replacing FAT/directory/file data; keep both boot sectors invalid; flush the
complete new tree; then publish the boot sectors. Existing final sync/release
persists publication. This adds two synchronizations and does not rely on old
and new file clusters coinciding. Flush failures stop without retry. The parent
rebased onto `a12aa3b` (the merged session-environment integration); other pins
are unchanged. Parent `52c177b` pins the published userland fix and records the
enabled default in both Kconfig and `.config`.

With the same nested-KVM/root USB/EHCI configuration and finalized saved-file
baseline, manual GDB stopped the real writer before reading the archive source
at offset 5242880 for 4096 bytes. The live image used rebased parent `bbe595f`
plus the enabled-default changes and published userland `a3ea1b0`; it reported
`bbe595f87ea4`. BOT had completed 2409 writes / 9852928 bytes and two flushes
(boot qualification and persisted boot invalidation). QEMU was killed while
held at that breakpoint. Host inspection observed:

- both primary and backup FAT boot sectors still entirely zero;
- the whole kernel matching the new source, and the first 5 MiB of archive
  matching the new archive while the complete archive still differed;
- every byte outside the actual GPT-derived ESP extent unchanged.

Fresh live `52c177b` media listed the same disk as eligible with damaged FAT32
geometry, installed revision `unknown`, and
`Boot files damaged or missing; Update will rebuild them.`
[The confirmation screenshot](review-recovery-confirmation.png) records that
classification. After typed `update`, a second manual debugger stop immediately
before primary boot-sector publication observed three flushes (qualification,
invalidation and complete-tree synchronization). The archive was then complete
while both boot sectors remained zero. Normal execution resumed, verified the
files, reopened the pool and reported `updated`, exit 0. Final BOT observations
were 11621 writes / 47578112 bytes and five flushes, with healthy transport and
no retries. Both outside-ESP comparisons passed again. The unchanged pool passed
host fsck without replay; extracted `kept.txt` retained its original SHA-256.

A separate blank 2 GiB USB target with the same `52c177b` live media passed
Install / Read the room, typed `wipe`, formatting, GPT rescan and boot-file/root
verification, reporting `installed`, exit 0. The repaired original disk also
booted alone from USB with no ISO or VirtIO disk, reported `52c177be45b3` and
npfs `system://`, and returned the unchanged original SHA-256 for `kept.txt`.
[The target-only boot screenshot](review-target-boot.png) records both results.
All follow-up QEMU/debugger processes were closed.

Ordinary full image builds passed for the integrated sources, including
`make -j16 image` without overrides. Kconfiglib inspection confirmed `XHCI=y`
both before loading any `.config` and after loading the checked-in configuration.
Existing third-party build warnings remain. No new tests, hooks or automation
were added. `fj actions tasks` in userland reported zero tasks; `fj pr status 124`
could not parse Forgejo's empty status (`unknown variant`, empty string), so
standalone dependency CI is unavailable rather than a passed check. Parent CI
builds the pinned dependency. Physical flush/power-loss qualification remains C.4.
