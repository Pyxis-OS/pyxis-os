# Read-only npfs FUSE qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

Manual validation on 2026-10-06 for [the Linux mount](../../npfs-linux-mount.md). Implementation and QEMU/loop-device
checks are complete, and the owner reported successful physical stick mounting and copying on Arch Linux the same
day, completing the remaining qualification step.

## Revisions and environment

Parent baseline `8544220` is main with the accepted milestone; the guest used pinned fs `d352c7e`, userland
`c3d5668` and ports `0ead472`. Host baseline readers were built from filesystem main `9e1a63f` (format and tool
source identical to the parent's pin). The new mount's executable source is fs `d5042f5`; published `3f527a4` adds
only runtime-package and dependency-license documentation, and the parent integration pins it.

Host Fedora 44, Linux `6.19.10-300.fc44.x86_64`, host GCC 16.2.1, Pyxis GCC 16.2.0, libfuse3 3.18.3. QEMU 10.2.2
with the AHCI correction, q35 nested KVM, `-cpu max`, four CPUs, 2 GiB, OVMF with a fresh variables copy. The 2 GiB
target was a disposable sparse file on tmpfs exposed through modern VirtIO block (512-byte sectors, writeback
cache) with VirtIO RNG and no network; the guest disabled xHCI and used the 30-second npfs flush interval. A full
current-main source build used the existing cross compiler, and the ordinary installed init mounted partition 2
read-write as `system://`:

```sh
make -j16 image fs-tools PYTHON=/usr/bin/python3
make -j16 image SPACES=pyxis=boot://init-installed MOUNT_DISK=6faa2e3f-2f4d-4b9b-9265-17fd889cd402
```

The validation-only `.config` change was restored. Host builds passed standalone `make -j16` and parent
`make -j16 fs-tools`; with `PKG_CONFIG=false` ordinary tools still built and explicit `make npfs-fuse` refused with
the development-package requirement. No tests, fixtures, fault hooks, automation or CI jobs were added; vendor
warnings remain and the host tools build with `-Werror`.

## Guest files and Linux readback

The target started as an earlier finalized installation with a synced 661-byte `kept.txt`. In the guest, ordinary
commands made two more files and a directory, synced the pool and reported SHA-256 values:

```text
cat boot://doom.pxe > system://doom.pxe
mkdir system://notes
cat boot://vi.pxe > system://notes/editor.pxe
sync system://
sha256sum system://kept.txt system://doom.pxe system://notes/editor.pxe
```

(`cp` is not packaged, so native `cat` redirection made the copies.) QEMU was stopped after the sync and hashes,
before Linux opened the source, and the pool was extracted unmodified:
`dd if=disk.raw of=pool.raw bs=4096 skip=131328 count=392955 conv=sparse`, `fsck.npfs --image pool.raw`, then
`npfs-fuse -f pool.raw MOUNTPOINT`. Host fsck reported `structural check passed`. FUSE listed `system/`, its files
and `notes/` owned by the mounting UID/GID with directory mode 0555 and file mode 0444, and Linux `sha256sum`
matched every value reported inside Pyxis:

| Path under `system/` | Bytes | SHA-256 |
| --- | ---: | --- |
| `kept.txt` | 661 | `aa947fd83f8021231c34df87f225c5d5e2db615b5041af7b84a7800dfb09c86d` |
| `doom.pxe` | 438384 | `58aaadfbc831a008f6363a423c79678cfa6d8c7c84e9c6aaed517867571d4966` |
| `notes/editor.pxe` | 97296 | `bdcc20ff1d2b1b8c9cb2a374bb8245e442c0b61a13f751afff942d940cdf8846` |

The mounted binaries also matched their boot-source copies with `cmp`, and a Linux `cp` of the nested file matched.
Seeking to four bytes before EOF and reading eight returned four bytes then zero. A write-open returned `EROFS`
(30), and a cooperating writable fsck open while mounted failed on the shared image lock before replay or any
write. The nested file's creation xattr was `1791278967125558420`; its modification xattr and Linux `st_mtime_ns`
were both `1791278967159648720`, and the synthetic mount root returned the literal `unknown` for its creation xattr
(observed payloads, not constants). Unknown inode timestamps, negative dates and signed endpoints were reviewed in
source, not created. After `fusermount3 -u` the pool's SHA-256 was unchanged
(`55c2d7c7118d5453bbcaf0e44a05ec90e2d5d49d57a00de3b9a4e44979e3000e`), and the mount exited successfully and released
its lock.

## Partition, multiple volumes and journal refusal

The stopped disk was attached with `losetup --find --show --read-only --partscan` and its npfs partition
(`/dev/loopNp2`, number allocated per host) mounted with `npfs-fuse -f`. It returned the same three hashes, and as
root gave UID/GID 0 and file mode 0444. The existing inspector refused this block-device source as invalid,
confirming its image-only contract, and the whole disk digest was unchanged after unmount and loop detach
(`a256d138fc7f096a3dbdcf746a609ba90b076fea2d058c44ebeeeaae66e1aa1b`). This exercises Linux block-device opening, not
physical USB.

A host-formatted 128 MiB image with `work` (importing the format headers) and an empty `empty` volume mounted with
both names, recursively listed the imported tree and empty volume, matched the original header copy and unmounted
normally. For journal refusal, the real committed transaction from the
[system-update recognition qualification](../system-updates-task1/README.md) (left by killing QEMU after journal
commit and before checkpoint home writes) was extracted to RAM, not mounted or replayed, and refused:

```text
npfs-fuse: committed journal; boot Pyxis once to recover it, or run
fsck.npfs --image COPY --replay on a copy of the image. Nothing was written.
```

Its digest was unchanged (`953befbea53cf2118693848313c4f62e960897515296db614445b0d9905b5793`). No journal controls
or payloads were manufactured.

## Reader comparison and limits

Three original-reader `npfs-inspect cat` runs and three Linux `cat` runs through the mount read the same
438384-byte file to `/dev/null` on RAM-backed storage; `/usr/bin/time` reported 0.00 s for every sample (below its
useful resolution), and reader peak RSS was 1608/1680/1604 KiB for inspection and 2100/2100/1960 KiB for Linux cat
(excluding the FUSE daemon, with file data already cached). These are coarse observations, not latency or memory
budgets or evidence of no regression; no profiling or physical-device measurement was done.

Independent read-only review found no blocking correctness finding, and its minor root I/O diagnostic issue was
fixed before publishing ([filesystem PR #30](https://git.internal/PyxisOS/pyxis-fs/pulls/30)). Sparse file holes,
very large directory continuation, resource exhaustion, malformed records and source-changing races were
source-reviewed, not fault-injected. The adapter requires an unchanged source and checks local namespace records,
not global allocation ownership; whole-pool checking remains fsck's job. The rebuilt builder was published and its
optional FUSE compilation qualified in the [task-2 record](../npfs-fuse-task2/README.md); an old builder's success
only checked the targets whose dependencies were present.
