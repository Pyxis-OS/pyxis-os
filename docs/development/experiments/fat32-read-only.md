# FAT32 task 1 qualification

## Revisions and configuration

Baseline captured before implementation: Pyxis `2f0b64d83d33`, userland
`d2437266d459`, fs `b427df29f8` and ports `df3210b04142`; lwIP
`a1aadb91a503` unchanged. The fs main merge `0ece2d7960f5` is tree-identical
to that pinned fs revision. Dependency heads for this task: fs
`b5003ab37e70`, userland `80da50931a25` (rebased onto main `95888f0b29bf`).
Integration code `8c06e8876a7f` is rebased onto Pyxis main `df7ec8ab` and keeps
that main's ports pin `28b5a3bce9aa`; no ports change belongs to this task.
Documentation follow-ups do not change the implementation.

Builder: `git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a`;
`make -j16 image BOOT_MENU_TIMEOUT=5`, native ELF debug links against the same
exported SDK, and `make -C fs -j16` all pass. The kernel does not link the new
reader yet. ASan/UBSan host qualification uses a separate ignored output directory:

```sh
make -C fs -j16 BUILD="$PWD/build/fat32-qualification/asan" \
  CFLAGS='-O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer' \
  "$PWD/build/fat32-qualification/asan/fat32-inspect"
```

Interactive QEMU: q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF, VirtIO SCSI
CD and VirtIO block scratch disk, native console tabs, GDB on a loopback port.
Inspection media are attached read-only. Installer recognition uses a writable
disposable copy but stops before confirming Update; no update writes are issued.
Unused host audio is disabled for later runs after one QEMU startup stalled
in host PipeWire before vCPU creation/guest serial output. That startup is not
parser evidence. No new tests, fault hooks, boot automation or klog messages.
Artifacts and one-off media edits stay ignored under `build/fat32-qualification`.

## Existing installer baseline and repeat

The existing installer prepared a disposable 2 GiB disk with a 512 MiB ESP,
1,534 MiB npfs pool and 128 MiB journal. It wrote/verified 2,211 bin files,
42,052,282 bytes. Its installed rescue archive is 35,804,276 bytes.
This preparation is a fixture, not a new installer throughput measurement.

Before implementation, interactive Update and a GDB breakpoint/finish at
`install_esp_inspect` returned `INSTALL_ESP_VALID`, revision `2f0b64d83d33`,
with no reason. After implementation/rebase, the same workload on a disposable
copy returned `INSTALL_ESP_VALID` and displayed the same installed revision.
Disk ID 1 and layout match: ESP byte offset 1,048,576/length 536,870,912,
pool byte offset 537,919,488/length 1,609,543,680. Byte comparison confirms that
recognition made no disk changes.

`userspace/installer/esp_read.c` remains byte-identical, SHA-256
`04211b00939921f6c14a713e9b8c2172c36d819910fa820cac74a7d8b4ccc778`.
It retains its lenient recovery and foreign-binding checks; the strict library
is not substituted into that path. No existing I/O implementation changed, so
this record compares recognition behavior, not invented before/after timings.

## Host media and refusals

Disposable sources are whole-disk GPT images; no mounted host volume or real
ESP is changed. `sgdisk --clear --new=1:2048:133119 --typecode=1:ef00` prepares
entry 1 in a 70 MiB image. A separate 64 MiB partition image uses
`mkfs.fat -F32 -S512 -s1 -f2 -h2048`; `mmd`/`mcopy` create nested directories,
empty files, mixed-case/long names and Unicode LFNs, then bytes are copied to
sector 2048. `sgdisk --verify` and `fsck.fat -n` pass. Additional images cover
one FAT and 128-sector clusters (a sparse 5 GiB FAT partition in a 6 GiB GPT
disk). The installer fixture covers eight-sector clusters and two FATs.

An independently edited valid surrogate pair corrects mtools' non-BMP truncation
in the Unicode fixture. An ASCII short alias addresses the Unicode LFN.
A 255-byte ASCII LFN uses 20 slots across directory sectors. A payload chain
is fragmented from cluster 5 through 1000 then 7, with the displaced cluster
freed and bytes copied independently. `fsck.fat -n` remains clean; mtools and
engine reads compare byte-exactly with the 10,496-byte source. Unicode alias
reads compare with the 26-byte text source. Final whole-volume counts are
five files, three directories, 28 allocated and 128,994 free clusters.
Valid stale FSInfo hints and differences confined to FAT upper reserved nibbles
remain accepted.

Forty-seven deliberately malformed variants refuse without stdout output or
sanitizer diagnostics. Edits cover GPT CRC/signature disagreement, overflow and
out-of-range header/array/partition fields, duplicate GUIDs/overlap (with valid
recomputed CRCs); BPB bounds and unsupported geometry/mirroring; backup/FSInfo
signatures and hints; FAT mirrors, root/directory/file cycles, short/overlong/free/
bad/reserved links, cross-links and orphan allocations; LFN ordinal/LAST/checksum/
type/cluster/filler/surrogate damage, alias collisions and invalid entries.
Invalid zero/non-power cluster sizes are reported unsupported; corrupt structures
report corrupt. Unsupported short-only OEM names also refuse explicitly.

Configured 1 KiB workspace, one step, one sector and depth one each fail without
output or reduced validation. `/dev/full` produces an output failure. Whole-file
buffering is absent; metadata preflight precedes sinks, but a later I/O/work/sink
failure can still leave a validated prefix as the API documents.

## Native QEMU and GDB

The inspection boot profile asks for disk ID 1 and GPT entry 1 explicitly. Native
`list /`, case-insensitive `read /long mixed-case file.txt` and `check` succeed;
the text is exact and Unicode names reach the terminal as UTF-8 (unsupported
terminal glyphs render replacements). The initial fixture reports four files,
three directories, 26 allocated and 128,996 free clusters; later host additions
are accounted for above.

GDB observes resource count two, roots/cwd count zero, namespace zero, explicit
console streams and disk rights 3 (INFO|READ), transport zero. After admission
the source adapter has first sector 2048/length 131,072 and maximum request eight
sectors. The operation has a 16,128-byte visited bitmap and depth 64. No DISKS,
MOUNT, write or child launcher reaches the inspector.

A root self-cycle returns CORRUPT from `check`; a lone low surrogate returns
CORRUPT from `list /`, without any entry output. A zero-length BPB volume fails
admission with CORRUPT and the child reports failure. On ordinary quit and on
that rejected-open path, GDB observes inspector/selector/root retirement on the
BSP, zero inspector tasks/storage, closed disk/streams and `destroy_process`
returning MM_OK for all three. Workspace is freed before a replacement allocation
or exit. Scratch/corrupt-image hashes match before/after native inspection.

A final image smoke check after the dependency/parent rebase also returns the
expanded fixture's five files, three directories and 28 allocated clusters.

These checks exercise userspace parsing and existing native reads in a nested VM.
They do not establish hardware behavior, native ESP qualification, future kernel
parser fault isolation, snapshots, exclusion, writes, recovery or mount lifetime.
