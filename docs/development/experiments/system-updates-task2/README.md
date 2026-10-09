# ESP update and recovery qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Manual validation on 2026-10-04, task 2 of [system updates](../../../userland/system-updates.md).

## Sources and environment

Baseline parent `91bf91d4a0fb968cf7acc1e1514645dc64dcac50` with userland `61cad34c0b7586b2acef40274f13385dc5b82837`.
Executable update and recovery sources were userland `6aad43b`; published `bb4fd660a449c5a572d4dd41e5e0bc7ce61ff7b5`
adds only the installer README. The live kernel reports `91bf91d4a0fb`. Unchanged pins: fs `d352c7e11c5e`, ports
`55b6f8edc0d3`, lwIP `a1aadb91a503`.

Host Linux `6.19.10-300.fc44.x86_64`, target GCC 16.2.0, QEMU 10.2.2 with the AHCI backend correction, q35 nested KVM,
`-cpu max`, four CPUs, 2 GiB, OVMF with a fresh variables copy per boot, disposable raw files on modern VirtIO block
(512-byte sectors, writeback cache), VirtIO RNG, xHCI disabled, 30-second npfs flush. The live source was a CD-ROM ISO
with a 60-second menu timeout for manual Limine selection and console input; target-only boots had no live medium.
These observations do not qualify physical hardware, USB raw authority, actual power loss or uncertain I/O failures.
Ordinary full source builds passed (`make -j16 image fs-tools BOOT_MENU_TIMEOUT=60`, including the two recovery-reader
follow-ups); the modified installer compiled without new warnings, and no tests, CI jobs, automation or
compiler-container rebuild were added or needed.

## Finalized installation and successful update

The 2 GiB finalized older fixture from the [recognition qualification](../system-updates-task1/README.md) had no
`SAFE_TO_WIPE` marker or revision record and held a synced 661-byte `system://kept.txt`; it had already booted
normally, and independent copies were used for normal Update and for interruption/recovery. The candidate displayed
installed `unknown`, live `91bf91d4a0fb` and disk GUID `6faa2e3f-2f4d-4b9b-9265-17fd889cd402`. Cancelling at typed
confirmation before write qualification left the raw image byte-identical to baseline. After typing `update`,
exclusive revalidation, writing, flush/release/rescan, FAT byte comparison and read-only system reopen completed; the
installer printed `updated` and exited 0.

The writer issued 46,021,632 bytes in 11,241 target writes and two flushes, and from confirmation through completion
revalidation, rescan and verification read 46,330,880 bytes in 11,331 requests (virtual block counters, not timing or
physical write amplification). The ESP revision became `91bf91d4a0fb` plus newline; the extracted `limine.conf` kept
the disk GUID, used timeout zero and installed init and had no Install entry. Comparing the first 1 MiB and every byte
from the pool start at 513 MiB through EOF showed **every byte outside the ESP unchanged**, including both GPT copies,
partition GUIDs, the whole pool and trailing slack (`cmp -n 1048576` and `cmp -i 537919488` against the baseline). Host
`fsck.npfs` of the extracted pool (`dd … bs=512 skip=1050624 count=3143640`) reported `structural check passed` and the
extracted `kept.txt` matched its source (SHA-256 `aa947fd83f8021231c34df87f225c5d5e2db615b5041af7b84a7800dfb09c86d`). A
target-only boot reached the ordinary local session, `fastfetch` reported Caelum `91bf91d4a0fb` and npfs `system://`,
and `cat system://kept.txt` displayed the retained file.

## Interrupted rewrite and recovery

An independent finalized copy was updated through typed confirmation with GDB on the matching live kernel ELF stopped at
`disk_perform` (`break disk_perform if job->operation == NPFS_RAW_WRITE && job->offset == 1056768`): the pending
operation was NPFS_RAW_WRITE at absolute offset 1,056,768 for 4,096 bytes, after two earlier writes (8,192 ESP bytes,
no flushes). QEMU was killed with SIGKILL while paused, before the third write, leaving both boot-sector copies cleared
by the real writer and mtools rejecting the ESP as non-DOS media; no control bytes were manufactured and no fault hook
was added, and the GPT and pool-to-EOF ranges were unchanged. A fresh live boot listed the disk as eligible with damaged
FAT32 geometry, installed revision `unknown`, and `Boot files damaged or missing; Update will rebuild them.` Typing
`update` reran normal eligibility under exclusive access and rebuilt the ESP (same 46,021,632 write bytes in 11,241
requests and two flushes, `updated`, exit 0). The first-MiB and pool-to-EOF comparisons passed again, host fsck passed,
`kept.txt` matched the same digest, and a second target-only boot reached the local session with `fastfetch` showing
revision `91bf91d4a0fb` and mounted `system://`.

## Matched candidate inspection counters

Three baseline and three changed-source inspections used the same healthy finalized fixture and QEMU configuration.
Counters were read before Update selection and at confirmation (cumulative values subtracted after backend resets); an
accidental normal boot before baseline sampling was excluded. The first two changed samples used `2dfe7da` and the
third `6aad43b`, which changes only damaged-directory handling.

| Sources | Read bytes per inspection (three samples) | Requests per inspection (three samples) | Writes / flushes |
| --- | --- | --- | --- |
| Baseline `61cad34` | 175616, 175616, 175616 | 49, 49, 49 | 0 / 0 |
| Changed `2dfe7da`, `6aad43b` | 175616, 175616, 175616 | 49, 49, 49 | 0 / 0 |

Each group had zero range, so healthy candidate inspection has the same virtual read count; this establishes no latency
or physical-device performance, and no profiling was enabled. Exclusive revalidation after confirmation is additional
work, followed by full ESP writing and byte verification.

## Submitted integration build and review

A full source build from parent `b67365d48cbd` and published userland `bb4fd66` passed with the five-second live menu;
its ISO recognized the updated target and displayed installed `91bf91d4a0fb` and live `b67365d48cbd`, and cancelling at
typed confirmation made zero writes or flushes. Build and filesystem CI passed for that parent revision; userland has no
Actions tasks (`fj pr status` could not parse its empty aggregate status and `fj actions tasks` returned zero).
Independent review found and then confirmed fixes for optional revision entry damage and for later directory or
long-name damage masking an already located foreign configuration: the reader now continues binding inspection through
known undamaged paths, keeps REBUILD diagnostics sticky and lets real I/O or allocation REFUSED errors stop inspection.
The final re-review found nothing remaining ([userland PR #120](https://git.internal/PyxisOS/pyxis-userland/pulls/120)).

## Limits

The killed process qualifies one real interrupted write prefix; it does not exhaust interruption points or qualify
hardware power-loss persistence. Runtime checks used 512-byte VirtIO sectors; 4096-byte geometry and uncertain I/O and
error branches were source-reviewed and built, not exercised. Update replaces the entire ESP, including unrelated ESP
files, and has no fallback. Physical installation and update remain deferred: writable USB block support exists, but
installer raw-disk authority remained VirtIO-only and needs separate integration.
