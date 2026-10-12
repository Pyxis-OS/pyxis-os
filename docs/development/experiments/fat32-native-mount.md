# Native read-only FAT32 mount qualification

Captured 2026-10-12. The [pre-task baseline](fat32-mount-baseline.md) uses
main `173f132141e0`. Implementation qualification uses parent `5857d0af58d2`
over main `7db4cb31`, fs `30f625a236dd`, userland `5d07ac8ad407`, ports
`fd1eb30f17f3` and unchanged lwIP `a1aadb91a503`. Main advanced through private
capture and static TLS/errno while this task was in progress. Rebuild the SDK,
kernel, userland, ports and default image with `make -j16 image
BOOT_MENU_TIMEOUT=5` in the published `pyxis-llvm23.1.3-2d23443` builder.
The final ABI-comment/documentation pass also rebuilds successfully. No compiler
rebuild, new test framework, fault hook or klog line.

QEMU 10.2.2: q35/KVM, host CPU, one or four CPUs, 1 GiB RAM, OVMF, modern
VirtIO block (`disable-legacy=on`), optional VirtIO SCSI CD, unused audio disabled.
Manual console interaction and GDB inspect native operations, ownership and
drainage. Private image/config copies select the trusted browsing profiles;
default image behaviour remains unchanged. All jobs are stopped after inspection.

## Matched costs

Run the existing host `fat32-inspect` check, list and read commands from the
baseline seven times on the same cached 70 MiB GPT fixture. Complete subprocess
median/min–max milliseconds, including Linux VM startup overhead:

| Operation | Original baseline | After |
| --- | --- | --- |
| Check | 1.467 / 1.335–1.626 | 1.699 / 1.434–2.250 |
| List | 0.474 / 0.455–1.017 | 0.482 / 0.448–0.885 |
| Read | 0.556 / 0.499–0.904 | 0.484 / 0.452–0.880 |

The higher check median did not reproduce in seven alternating invocations of
the preserved baseline and current binaries: 1.392 / 1.364–1.728 versus
1.377 / 1.349–2.909 ms. The existing check implementation is unchanged. These
small startup-inclusive samples do not establish a filesystem speedup.

Repeat the same four-CPU npfs fixture/configuration and command, with profiling
off and GDB detached during timing:

```text
iobench read system://iobench-small.bin --bytes 32768 --rounds 5
```

All rounds verify their payload and EOF with zero failures. The original baseline
complete-pass median/range was 52.716 / 48.179–75.426 ms. To distinguish changed
host conditions, also reboot its preserved exact-revision image under today's
configuration. Current-after, original-control twice, then current-after again:

| Image/run | Open median ms | Payload median ms | Complete median / range ms |
| --- | ---: | ---: | --- |
| Current 1 | 0.441 | 4.378 | 5.781 / 5.216–6.468 |
| Original control 1 | 0.431 | 4.060 | 5.488 / 5.210–6.322 |
| Original control 2 | 0.467 | 3.722 | 5.037 / 4.828–6.050 |
| Current 2 | 0.440 | 3.907 | 5.396 / 5.233–6.063 |

The much lower absolute cost also occurs on the unchanged original image. Current
run medians are modestly higher than these controls, with overlapping complete
ranges and intervening main/toolchain changes; these nested-VM samples neither
isolate an adapter regression nor prove no regression. npfs retains its pool and
one wrapper after shell exit; no FAT workspace/extent is introduced by plain use.

## Native objects, exclusion and drainage

Use the baseline's disposable 134 MiB disk: FAT entry 1 at sector 2048 / 131072
sectors, npfs entry 2 at 133120 / 131072. Ordinary delegated `ls /` and
`cat /dir/child/hello.txt` match the raw inspector/mtools contents. Public native
disk/mount helpers executed under GDB establish:

- Two exact FAT opens share domain and one 24416-byte workspace. Independently
  narrowed control ceilings reject widening; READ-only disk grants deny admission.
- Held long-name and ASCII-alias FILEs have the same real identity, SIZE=12 and no
  MTIME. Offset 1 / length 4 returns `ello`; beyond-EOF returns zero bytes. A
  separate mtools-prepared copy gives distinct identities to two empty files,
  lists a UTF-8 LFN and reads it through its ASCII alias. Capturing/launching its
  `ls.pxe` through the existing launcher succeeds and lists the delegated root.
- Held roots or FILEs make RETIRE return BUSY. Drained retirement releases FAT
  workspace/extent; its control observes a tombstone, retires idempotently and
  refuses OPEN_ROOT. Remount assigns a new domain. Closing every control instead
  retains the published state; authorized reacquisition keeps that domain.
- A legitimate RW disk object opened/released before mounting cannot claim the
  mounted FAT partition or the whole disk. A disjoint partition remains claimable
  and supports npfs admission. Cross-backend overlapping mounts return BUSY.
  After FAT retirement, its partition becomes claimable; retained npfs still
  blocks whole-disk claims. No writes are submitted.

GDB calls use existing mapped functions and a mapped return breakpoint; normal
GDB return trampolines fault on NX stacks here. No stack is made executable.
At pending admission, the actual worker owns the extent reservation and workspace,
with domain/data/control still zero. In a one-CPU kernel-space/IF=0 context with
the relevant locks free, invoking the existing process-stop routine requests
termination of the waiting browser. After normal worker completion and process
retirement, FAT instance/workspace/wrapper counts and registry are zero. This is
debugger qualification of process stop, not a Ctrl+C or group-supervisor test.

Existing disposable hidden-entry-after-end-marker and invalid-volume-label images
refuse native admission with IO; the 68-directory fixture refuses with LIMIT at
depth 64. Four-CPU post-failure inspection finds no mounts, backing, wrappers or
reserved extents. Full disk hashes remain unchanged, including the writable
attachment used only for legitimate CLAIM/RELEASE checks.

## Boot-bound ESP and limits

Boot a disposable 256 MiB GPT disk directly through OVMF, with a 128 MiB ESP
containing the rebuilt kernel/archive and a disjoint npfs partition. `mkfs.fat`,
mtools and `fsck.fat -n` prepare/check it independently. The trusted issuer matches
the owned, normalized Limine disk/partition UUIDs; four-CPU admission uses 40560
workspace bytes. Delegated `cat /boot/limine/limine.conf` succeeds; drained quit
leaves zero FAT counts/extents. Two matching copies refuse before admission.
Optical boot with that ESP attached supplies no ESP issuer and refuses without
fallback. Media hashes remain unchanged.

Failed reply-copy/escaped-sibling preservation and group-bearing deferred-cleanup
ordering are source-reviewed, not separately induced. Allocation failure,
deadline expiry, device loss and native USB/NVMe/ESP hardware are not separately
qualified here. No writable mount, installed binding/editor, external mutation
snapshot or durability claim is added. Owner-routed native checks remain later.
Raw logs, images and debugger transcripts stay local.
