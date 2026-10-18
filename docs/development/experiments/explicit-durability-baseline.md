# Explicit durability baseline

Task 1 of [explicit durability](../../wip/explicit-durability.md), captured
2026-10-18 before any change, and the same workloads [after task 2](#after-task-2).
These are nested-VM QEMU measurements; the stick baseline needs an owner run (see
[native run](#native-run)).

## Revisions and configuration

- Main `82f9387d` (userland `3677edd`, ports `3366103`, fs `2533799`, lwIP
  `a1aadb9`), built with `make -j12 image fs-tools` in the published
  `pyxis-llvm23.1.3-abc00d0` builder. No compiler-container rebuild; the only
  warnings are the existing vendor port warnings.
- Update workload ISOs: three builds of the same tree with
  `BOOT_MENU_TIMEOUT=60`, differing only in kernel revision: `b7a6ff5f7218`
  (a local empty commit, never pushed), `489b5a25cbb7` and `82f9387dc1d8`.
- Payload workload ISO: the same tree with
  `MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab` and a temporary, uncommitted
  `config/installed.lua` mounting partition 1's `work` volume read-write in a
  `remote` space with `launch = true` (the [native Clang](../hosted-tools.md#pool-and-authority)
  layout with `work` in place of `toolchain`).
- Host: Fedora 44 guest VM (kernel 7.2.9) on KVM, so every QEMU run is nested.
  QEMU 10.2.2 with the local AHCI fix, `q35`, `-accel kvm -cpu max`, four vCPUs,
  4 GiB, fresh OVMF variables, VirtIO RNG and net, read-only ISO on VirtIO-SCSI,
  `-display none`.
- Target: one VirtIO block disk, a raw file on the VM's btrfs disk (fixtures
  restored as reflink copies), `cache=writeback` so guest flushes reach
  `fdatasync`, 512-byte sectors, 64 KiB transfers. The file deliberately lives on real storage rather than
  tmpfs, where flushes cost almost nothing. A 4 KiB `dd oflag=dsync` on that
  filesystem takes about 4.3 ms per write.
- Timing: host timestamps on each serial line (the installer's progress records
  reach Caelum's log, which goes to serial), QMP `query-blockstats` sampled every
  0.25 s for the target's flush, write and read counters, and host timestamps
  around each Remote command (`pyxis-remote --machine`, waiting for
  `command_complete`). Device counters include asynchronous writeback in the
  same window.
- Layout: `fsck.npfs`, `npfs-inspect` and `mshowfat` (mtools) on the stopped
  images, plus a local read-only probe over the fs repository's host decoders
  (`npfs_map_block`) counting each file's physical block runs. The probe is not
  committed.

## Update: old revision cleanup and FAT32 staging

Fixture: a blank 4 GiB disk installed from `b7a6ff5f7218` (classic, Read the
room, default 28 MiB journal), then updated with `489b5a25cbb7` and restarted
through the installer (pools flushed and checkpointed; host `fsck.npfs` passes
with an EMPTY journal). It holds both revisions, about 2,400 entries each.

Each sample restores that fixture, boots `82f9387dc1d8`'s Install entry and runs
classic Update (`classic`, `2`, apply, `update`), so the installer copies the new
revision, keeps the previous one and removes `b7a6ff5f7218`: the real cleanup
path (`install_programs_cleanup`). Three samples:

| Phase | Wall seconds, median (range) | Device flushes | Writes | MiB written |
| --- | ---: | ---: | ---: | ---: |
| Copying programs to bin (2,237 files, 128 directories, 46 MiB) | 103.7 (91.8–119.7) | 18,460–18,509 | 32,826–32,935 | 196–198 |
| Flushing program pool | 0.44 (0.34–0.46) | | | |
| Staging `caelum.elf` on FAT32 (6.3 MiB) | 7.9 (7.0–9.6) | 18–22 | 114–117 | 6.0 |
| Staging `initrd.cpio` on FAT32 (38.3 MiB) | 42.8 (42.8–50.1) | 114–116 | 722–730 | 38.1–38.4 |
| Removing older program revision entries (2,366) | 93.4 (92.9–107.6) | 19,149–19,200 | 35,708–35,805 | 186 |
| Flushing program cleanup | 0.008 (0.006–0.009) | | | |

The console's own rate for the archive staging was 802 KiB/s on the fixture
run, about 0.9 MiB/s here (the owner saw about 1 MiB/s on vela).

What the counters show:

- **npfs namespace cost.** Removal issues about 8.1 flushes per entry, not the
  4 the technical-debt entry estimates: by code inspection each remove
  checkpoints the previous transaction and commits its own (4 flushes), and each
  removed file's detached record is then reclaimed by its own `cleanup_one`
  transaction (another checkpoint and commit). Copying costs about 7.8 flushes
  per entry: the create commits, then the file's delayed writeback commits its
  own transaction. Copying 46 MiB writes 196 MiB, and removing a revision writes
  186 MiB, because every transaction writes its metadata images twice, to the
  journal and then home.
  The final syncs cost almost nothing because everything is already durable.
- **FAT32 staging is not flush-bound in QEMU.** The 38 MiB archive took only
  114–116 flushes and about 725 writes, but 81,381–81,539 reads averaging 582
  bytes: about 2,000 single-sector reads for every 1 MiB growing write, constant
  across the file. The volume's 130,812 clusters need about 1,022 sectors of
  entries in each of the two FAT copies, so this matches reading both FATs sector by sector once per call. The code path is
  not attributed here; task 3 should find it before relying on batched phases
  alone. On a stick, where small reads are also slow, this may be a large part
  of the observed 1 MiB/s.

Fragmentation of the result:

- **npfs:** the new revision's 2,238 files (13,254 blocks) sit in 2,391–2,395
  physical runs; 141–143 files have more than one run, at most six (Neovim's
  1,894-block `nvim.pxe`). Writeback allocates at most 128 mappings per
  transaction, interleaved with inode and indirect blocks.
- **FAT32:** every classic file is one contiguous chain (for example
  `initrd.cpio` clusters 1711–11514). Update overwrites the existing fixed-path
  chains in place and truncates afterwards, so this flow cannot fragment while
  the archive does not grow; a fresh install writes one file at a time.

## Hosted payload extraction on npfs

The 2026-10-17 hosted payload (`payload.tar`, 369,326,080 bytes, SHA-256
`a7b786b1…5f3d54c`, built from `44e39884`): 8,701 files and 499 directories,
362,432,618 file bytes. A host-made disk holds one 2 GiB pool with a 16 MiB
journal (the installer's default for that size) and the archive in volume
`work`:

```sh
mkfs.npfs --image ppool.raw --size 2GiB --journal 16MiB --volume work --source psrc
truncate -s $((2*1024*1024*1024 + 2*1024*1024)) pdisk.raw
sgdisk --clear --set-alignment=1 --new=1:2048:+2G \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab pdisk.raw
dd if=ppool.raw of=pdisk.raw bs=1M seek=1 conv=notrunc,sparse
```

Each sample boots a fresh copy and runs, one Remote command at a time, the
[publication](../hosted-tools.md#manual-immutable-publication) steps that touch
npfs:

```text
cp work://payload.tar tmp://payload.tar    (preparation, not compared)
mkdir work://x
cd work://x
tar xf tmp://payload.tar
sync work://x
poweroff
```

| Command | Sample 1 | Sample 2 | Sample 3 |
| --- | ---: | ---: | ---: |
| `tar xf`, seconds | 1,127.4 | 1,074.7 | 1,654.5 |
| `tar xf` device flushes | 73,764 | 73,660 | 73,733 |
| `tar xf` writes / MiB written | 133,107 / 953 | 132,849 / 950 | 133,045 / 952 |
| `sync work://x`, seconds | 0.23 | 0.66 | 0.37 |

Extraction costs about 8 flushes per created entry (create commit, then the
file's own writeback commit) and writes 2.7 times the payload. The counters
barely vary but the third sample's time does: the guest did the same work while
the host's flush latency changed (other agents shared the host; their load was
not recorded). For scale, the
owner's native publication of the smaller 2026-10-13 payload took 332.9 s.
The final `sync` is cheap because writeback has already committed everything.
After `poweroff` every disk passed `fsck.npfs` with an EMPTY journal.

Fragmentation: the 8,701 files (93,287 blocks) sit in 9,217–9,222 runs; 371–373
files have more than one run. The four large executables account for most:
`clang.pxe` (22,068 blocks) in 46 runs, both `lld.pxe` copies (13,618) in 30,
`clangd.pxe` (10,842) in 25, so about 470 blocks (1.9 MiB) per run.

## After task 2

Task 2 at kernel `77f870b9e625` with userland `32a23f4` against main `a29372f8`
(userland `22a3ee7`), both built in the same builder with the configuration
above. The program set changed slightly since the baseline (2,240 files), so
main was measured again; the kernel's storage code had not changed. Update
samples alternated main and branch on the same fixture:

| Phase, three samples each | Main: seconds, median (range) | Main flushes | Task 2: seconds | Task 2 flushes |
| --- | ---: | ---: | ---: | ---: |
| Copying programs to bin | 100.4 (91.3–105.2) | 18,517–18,589 | 63.6 (50.5–70.8) | 9,044–9,089 |
| Staging `initrd.cpio` on FAT32 | 46.7 (42.9–52.4) | 114 | 43.0 (42.6–44.7) | 114 |
| Removing older program revision entries (2,366) | 95.9 (93.9–103.5) | 19,157–19,188 | 3.6 (3.3–3.8) | 12–16 |

Removal now writes 250–333 requests (2.6–3.5 MiB) instead of about 35,700
(186 MiB). Copying halves its flushes and writes (16,600 requests, 114 MiB);
what remains is file writeback, which still checkpoints and commits once per
file when the data cache fills. FAT32 staging is task 3's.

The payload extraction, run the same way on a fresh disk each time:

| `tar xf` | Seconds | Flushes | Writes | MiB written |
| --- | ---: | ---: | ---: | ---: |
| Main, same session (one sample) | 881.6 | 73,762 | 133,063 | 953 |
| Task 2, three samples | 776.4, 834.0, 1,219.9 | 36,947–36,952 | 70,990–71,043 | 638–639 |

The flushes, writes and bytes halve: namespace operations no longer commit,
and what remains is one writeback commit per file. Elapsed time moves much less
in QEMU, where these flushes are cheap; the 1,219.9-second sample ran while
another agent's compiler build saturated the host. The final `sync` stays
cheap, every disk passed `fsck.npfs` after `poweroff`, and file layout is
unchanged (9,219–9,224 runs, 373–374 files split).

Crash checks (decision B), each by `SIGKILL` of the VM, then host
`fsck.npfs --replay` (passed every time), `npfs-inspect` and a comparison with
the source:

- Unsynced `mkdir`, writes, copy and rename killed within the 30-second interval:
  none of them present. After `sync`: all present, the copy byte-identical.
- An unsynced replacing rename over a synced file: the target keeps its old
  contents and the moved file is complete under its old name.
- Old revision cleanup killed after 512, 1,216 and 2,112 removed entries: the
  old revision lost 280, 280 and 1,571 whole entries; the new revision is
  complete and sampled programs match the ISO.
- Extraction killed 60 s and 300 s into `tar xf`: every file present is complete
  or a whole-block prefix of its source (433 plus one prefix; 2,852). Killed
  after `tar` finished but before `sync`: all 8,701 files complete, made durable
  by the background commit.
- A kill timed into a COMMITTED journal (the VM paused until the on-disk control
  read COMMITTED): replay was required and succeeded; 107 complete files, one
  prefix and 123 created files still empty, none mismatched.
- Saved shell history on a freshly updated installed home: two lines typed,
  killed about four seconds later: both lines present, the space-prefixed line
  absent, no temporary file left. The same kernel with the previous history save
  (no sync) left an empty `.history` and both lines in a stray `.history.HEX`:
  the first save's rename does not flush, and nothing committed the second.

These cover chosen points, not every commit boundary, on emulated storage.

## After task 2b

Writeback in the running transaction at kernel `5811ee962eeb` against main
`7ffc03f6` (task 2 merged), same configuration, fixture and procedure; Update
samples alternated branch and main. A second VM briefly ran the extraction
crash repeats beside the first samples.

| Phase, three samples each | Main: seconds, median (range) | Main flushes | Task 2b: seconds | Task 2b flushes |
| --- | ---: | ---: | ---: | ---: |
| Copying programs to bin (2,241 files) | 55.9 (51.7–58.9) | 9,036 | 11.5 (10.1–26.6) | 16–20 |
| Removing older program revision entries (2,366) | 3.7 (3.6–3.7) | 12–14 | 3.7 (3.6–5.5) | 12–16 |
| Staging `initrd.cpio` on FAT32 (50.7 MiB) | 44.6 (43.4–50.8) | 154–160 | 54.0 (43.4–56.7) | 150–164 |

Copying now writes about 2,980 requests and 52 MiB for 46 MiB of programs,
against 16,552 requests and 114 MiB on main. The 26.6-second sample overlapped
a second VM running crash checks. Removal and FAT32 staging are unaffected.

| `tar xf` | Seconds | Flushes | Writes | MiB written |
| --- | ---: | ---: | ---: | ---: |
| Task 2 (above) | 776.4, 834.0, 1,219.9 | 36,947–36,952 | 70,990–71,043 | 638–639 |
| Task 2b | 753.8 | 120 | 14,792 | 379 |

PAYLOAD_NOTE

Checks (decision C), each by `SIGKILL` of the VM, then `fsck.npfs --replay`
(passed every time) and comparison with the source:

- Stale data: a synced file was removed, more than 4 MiB of new data written
  without sync, and the VM killed. All 239 blocks of the new data were on the
  device, yet the removed file (its removal never committed) was byte-identical:
  freed blocks were not reused before a checkpoint.
- Cleanup killed after 736 and 1,952 removed entries: whole entries removed, the
  new revision complete, sampled programs identical to the ISO.
- Extraction killed at 45, 60 (twice) and 240 seconds, and once timed into a
  COMMITTED journal (replay required and succeeded): every file present is
  complete or a prefix of its source. A writeback step that covers all of a
  file's cached blocks stages its current size, so a prefix can end mid-block
  (`lld.pxe` at 17,519,520 bytes, matching the source). The first 60-second run
  showed such a file (48,164,400 bytes) before the check accepted unaligned
  prefixes; its disk was overwritten before the bytes were compared, so that
  point was repeated.
- Saved shell history on a freshly updated installed home: both typed lines
  present after a kill four seconds later.

## Native run

Native stick numbers need the owner. The run sheet is
`/shared/present/durability-baseline/README.md`: an Update from the ThinkPad's
PXE archive onto the installed stick with UDP logging (host receive timestamps
on the installer's phase records) and, optionally, the payload extraction over
Remote. Task 4 repeats these QEMU workloads and the native run after each
change.

## Limits

- Nested-VM QEMU on a host file: absolute times say little about a USB stick.
  Flush latency here is about 4 ms; sticks vary far more.
- Three samples per workload, on a shared host where other agents' builds and
  VMs ran. Another agent's `pkill` stopped one Update sample and one fixture
  attempt; both were repeated from fresh fixtures.
- Phase boundaries come from progress records and include the installer's own
  CPU work (hashing, verification) inside each phase.
- No crash, power-loss or failure path was exercised.
