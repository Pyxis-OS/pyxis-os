# FAT32 directory-validation fixes

## Revisions and method

Before editing, build current Pyxis main `a90a57c98de8` with fs `b5003ab37e70`,
userland `2ccb58eb8246`, ports `28b5a3bce9aa` and lwIP `a1aadb91a503`.
The fixed shared reader is fs `9b4479e8e5ec`; other dependencies are unchanged.
Builds use `make -C fs -j16` and `make -j16 image BOOT_MENU_TIMEOUT=5` in
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a`. A separate
ignored host build uses `-O1 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer`.
No userland source, public ABI, installer reader, write API, test harness or CI
workflow changes. Daybreak's supplied images/probe remain outside the repository.

The source is a disposable 70 MiB GPT disk, selected entry 1 at sector 2048,
containing a 64 MiB FAT32 partition: 512-byte sectors, one-sector clusters,
two mirrored FATs. `mtools` created two files and three directories. Before
editing, `check` accepts the valid baseline and all three reported bad variants.
The pristine host executable and exact-main ISO/kernel are saved in ignored
`build/fat32-validation/before`; fixed artifacts/results stay under `after`.

## Refusals and compatibility

After the fix, `check`, `list /` and `read /DIR/CHILD/HELLO.TXT` return CORRUPT
with zero stdout for a live slot after the end marker, an illegal slash in the
root volume label, and VOLUME_ID|READ_ONLY label attributes. The check also refuses
nonzero/deleted first bytes in a later allocated directory cluster. A fully zero
later cluster is accepted, as are valid label spaces and nonzero unused entry
body bytes when the first byte is zero. Only the entry first byte is required to
be zero after the end marker; free data is not interpreted as a live record.

The retained adversarial cases still refuse loops/cross-links, malformed LFNs,
invalid BPB/FSInfo/GPT fields and depth exhaustion. Valid boundary/long-name cases
stay valid. Header boot code and unused FSInfo reserved bytes are not interpreted
as live metadata. Source sector/work/depth budgets remain in force for the extra
tail scan. ASan/UBSan reports no diagnostic on the affected check/list/read cases.

`fsck.fat -n` independently counts the hidden record while the old reader did not;
it reports the illegal label problems. It tolerates both embedded-space variants.
The [Microsoft FAT specification](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf),
page 30, explicitly permits short-name spaces. The read profile keeps them exactly:
`HE LLO.TXT` and `HELLO. XT` both read byte-exactly against the 12-byte source.
There is no normalization, adoption or new mutation permission.

## Matched cached host-VM costs

After retaining the baseline binary, run before/after pairs in B/A/B/A/B/A order,
100 CLI invocations per sample, same valid image and discarded stdout. Commands:

```sh
fat32-inspect --image BASE.img --gpt-partition 1 check
fat32-inspect --image BASE.img --gpt-partition 1 list /
fat32-inspect --image BASE.img --gpt-partition 1 read /DIR/CHILD/HELLO.TXT
```

Wall seconds from `time.perf_counter`, including process startup, on the Linux
codex VM with warm file cache; these are not physical-storage or native I/O costs.

| 100 invocations | Before samples (s) | After samples (s) |
| --- | --- | --- |
| check | 0.1683, 0.2174, 0.1655 | 0.1756, 0.2240, 0.1802 |
| list / | 0.0580, 0.0683, 0.0595 | 0.0607, 0.0586, 0.0604 |
| read | 0.0641, 0.0655, 0.0700 | 0.0693, 0.0661, 0.0687 |

Median check cost rises about 7%, from 1.68 to 1.80 ms per invocation. Short-run
ranges overlap; the implementation adds required bounded tail scanning, including
validation before output. Do not claim unchanged throughput or extrapolate these
cached samples to native hardware. No optimization work beyond the validation fix.

## Native qualification

Interactive QEMU: q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF, VirtIO SCSI CD,
read-only VirtIO block cases and disabled unused audio, native console and GDB.
Existing PCI discovery registers these four devices in reverse slot order; select
IDs using the serial PCI records rather than the command-line drive order.
The clean control is ID 1 and reports two files, three directories, five allocated
clusters. Hidden-tail ID 4 returns CORRUPT from `check`; GDB observes
`fat32_check` returning FAT32_CORRUPT on the selected partition.

Illegal-label-name ID 3 and illegal-label-attributes ID 2 also return CORRUPT
without a check report. The inspector returns to its command prompt and exits
normally on quit. All four original case images keep their pre-run SHA-256.
QEMU/GDB are stopped. No writes, mounts, repair or native hardware qualification
occur in this fix.

The integration PR rebases onto main `79a23845` before submission; the intervening
main changes are Synaptics input and documentation, with the same dependency
pins. The runtime results above remain tied to the recorded `a90a57c9` build.
