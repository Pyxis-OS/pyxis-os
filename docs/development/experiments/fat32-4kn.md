# FAT32 logical-sector qualification

Owner-accepted prerequisite for NVMe coexistence task 3, 2026-10-15. Qualified
code: parent `eca6e57a`, userland `8bde67b7`, fs `35582347` (based on alpha's
read-proof revision `22eaf447`); ports/lwIP unchanged. Ordinary source
`make -j8 image` passes with builder `df87791`; no compiler-container rebuild.
Independent source review found no concrete defect in the geometry changes.

## Control and media

Before the extension, the frozen main `14f900785097` image (userland
`f239d4796b79`, fs `101e1f68`) sees the valid 4Kn GPT but native RO admission
returns UNAVAILABLE. The complete disk hash stays unchanged. This establishes
the unsupported boundary, not a matched timing comparison. No throughput or
performance-neutrality claim is made here; the task-3 installer baseline and
after-runs remain separate.

Private disposable GPT media: 1 GiB disk, one 512 MiB ESP at 1 MiB, 4096-byte
logical sectors, 128 GPT slots, agreeing headers/arrays. Format with
`mkfs.fat -F32 -S4096 -s1 -f2 -h256`, populate 40 long-name text files and an
8 MiB payload with `mcopy`, then validate with `fsck.fat -n`. A separate 512-byte
control uses a 128 MiB ESP in the existing 2 GiB qualification image. GUIDs are
private random fixture identities, not native-device selections.

## Host checks

GCC 16.2.1 `make -j8 all` passes. The host inspector uses explicit
`--image IMAGE --gpt-partition 1 --sector-size 4096` with `info`, `check`,
`list /` and `read /payload.bin`; all pass and the payload hash matches its
independent source. Raw-writer sessions on disposable 512/4096-byte copies
cover mkdir, create, append, extension to 8193 bytes, replacement and reopened
CHECK. Independent `mcopy` compares exact content and zero-filled gaps;
`fsck.fat -vn` exits zero. Unsupported 1024/2048-byte requests and mismatched
BPB/source sector sizes refuse without changing the images. Originals remain
unchanged.

## Native adapter in QEMU

QEMU 10.2.2, Q35/nested KVM, host CPU, four vCPUs, 1 GiB, OVMF, modern VirtIO
block with `cache=none`, explicit matching logical/physical sector sizes,
VirtIO entropy and xHCI keyboard; no NIC/audio. Private boot entries select the
existing RO or scratch-RW FAT mount bootstrap with `boot.install=1`.

RO admission, enumeration and reading the last long-name file succeed on 4Kn.
GDB observes sector size 4096, one sector per cluster, 162928-byte workspace and
16-byte mirror map. The original disk remains unchanged.

RW checks cover long-name directory growth across the 128-slot sector boundary,
a write at offset 4095, extension to 8193 bytes with zero filling, held reads
after rename, and held-file reads after unlink/recreation. BEGIN followed by
CHECK succeeds; the observed interval is ended explicitly. This is not publisher
qualification under the 30-second lease. Sync and retirement return OK.
GDB observes a writable unstopped mount and actual 4096-byte geometry; workspace
is 375568 bytes after the small writes. After all handles close, instance count
and charged workspace are both zero. The 512-byte native run separately passes
cross-sector writing at offset 511, extension/zero filling, sync and retirement,
with zero retained instances/backing afterward.

Offline 4Kn CHECK reports 46 files, one directory and 2093 allocated clusters.
`fsck.fat -vn` exits zero; `mcopy` independently recovers the exact 8193-byte
cross-sector/zero-fill result. The original 8 MiB payload hash is unchanged.
Every byte outside the selected ESP matches the input. Independent extraction
and fsck/content comparison also pass for the 512-byte native result.

## Bounds and limits

GPT stays at 256 entries/64 KiB. Local FAT windows remain 4 KiB; full-check
windows stay at most 64 KiB per copy. Prepared images use actual sector bytes;
source-inspected accounting on 64-bit builds is 536 bytes per prepared 512-byte
image versus 528 before, and 4120 bytes per 4096-byte image. The above backing
observations are mount workspace, not RSS or a peak measurement.

No new test infrastructure, native disk/ESP write or hardware qualification.
Classic/coexistence executor integration and full installer after-runs remain
pending. Raw captures and disposable media stay in ignored local build output;
all task-owned QEMU/GDB/build jobs are stopped after qualification.
