# Read-only RAM journal replay qualification

Manual validation on 2026-10-06 for [read-only RAM replay](../../npfs-linux-mount.md), including the merged
filesystem #30 / Pyxis #437 review follow-ups. Both milestone tasks are complete: the owner confirmed physical stick
mount and copy on Arch Linux, and the rebuilt builder's optional FUSE compilation is qualified below.

## Revisions and environment

Filesystem baseline `77a8c33` is merged task 1; implementation `375b569` adds the shared journal loader, RAM overlay
and lookup change, and published `b427df2` adds only documentation since (the parent pins it). The guest used main
`7245570f8c76`, userland `c3d5668`, ports `0ead472` and the installed-init program; RAM replay needs no kernel, ABI,
format or target userland change.

Host Fedora 44, Linux `6.19.10-300.fc44.x86_64`, host GCC 16.2.1, cross GCC 16.2.0, libfuse3 3.18.3. QEMU 10.2.2
with the AHCI correction, q35 nested KVM, `-cpu max`, four CPUs, 2 GiB, OVMF with fresh variables, modern VirtIO
block (512-byte sectors, writeback cache) and VirtIO RNG; the guest disabled xHCI, kept the 30-second npfs flush
interval and used a 2 GiB disposable sparse disk on tmpfs. Standalone `make -j16`, parent `make -j16 fs-tools` and an
ordinary source image build passed (a fresh build first hit the host's `/tmp` quota unpacking a vendor dependency and
was redone in the existing worktree; no prebuilt bundle was substituted), using
`SPACES=pyxis=boot://init-installed` and `MOUNT_DISK=6faa2e3f-2f4d-4b9b-9265-17fd889cd402`. `make -j16
PKG_CONFIG=false` built the ordinary host tools without libfuse; host builds use `-Werror`; no tests, fixtures, fault
hooks or automation were added.

The newly published `pyxis-builder:pyxis-gcc16.2-binutils2.47` (digest
`sha256:e9af03bd0bd0ffb5e31651d1e864d6b42a157009fd948f105c7331afb5bf0ec4`; libfuse3 3.17.2, cross GCC 16.2.0,
Binutils 2.47.20260726) passed `make -j16`, including `npfs-fuse`, in a network-disabled container with the published
filesystem source read-only and a separate output directory, and its linked libfuse resolved. This confirms the
optional target build in the updated builder, not a FUSE mount inside the container; a successful job in the old
builder could skip that target.

## Interrupted current-main write

The target started from task 1's synced installation. Interactive guest commands reported the source hash, copied a
file and requested durability (`sha256sum boot://vi.pxe`, `cat boot://vi.pxe > system://replayed.pxe`,
`sync system://`). GDB with the matching ELF and `break checkpoint if pool->control.state == 1` let the first hit (the
file's creation transaction, sequence 20, two images) checkpoint; at the second hit, before the home-write loop, QEMU
was killed while paused. The control was COMMITTED at sequence 22 with three images, one descriptor block and payload
CRC `3851112063`; the staged inode was number 12 with size 97,296 bytes, with staged home targets bitmap block 1,
indirect block 4267 and inode block 4117, and the ordered file data had reached the source before the committed
metadata (observed values, not expectations).

The stopped pool was extracted (`dd … bs=4096 skip=131328 count=392955 conv=sparse`) and mounted with
`npfs-fuse -f committed-pool.raw MOUNTPOINT`. FUSE reported its in-memory replay and exposed the complete 97,296-byte
file in mode 0444 with SHA-256 `bdcc20ff1d2b1b8c9cb2a374bb8245e442c0b61a13f751afff942d940cdf8846`, matching the guest's
source value and a `cmp` against the boot-source copy. A separate writable copy recovered with `fsck.npfs --replay`
(`structural check passed`) gave an extracted file matching the RAM view and a clean `diff -r` between both mounted
namespaces, while the image-only inspector still refused the original with `journal replay required`. The committed
pool's SHA-256 was unchanged after unmount (`e00d59f23eab7172bd37523945aebe726772179f9d070040a0b4f95348a8aa96`), and
task 1's real committed-create image also mounted in RAM with `pending.txt` and matched a writable-fsck copy (source
digest `953befbea53cf2118693848313c4f62e960897515296db614445b0d9905b5793` unchanged).

## Partition access and writable copied files

The stopped disk was attached with `losetup --read-only --partscan`; following the
[device recipe](../../../../fs/docs/npfs-host-tools.md), the npfs partition's ACL was saved, read access granted to
UID 1000 and FUSE mounted as that user (read-only, private to UID/GID 1000). It exposed the same file and hash, and
`cp --no-preserve=mode` produced a matching 97,296-byte file owned by 1000:1000 with mode 0644. The saved ACL was
restored byte for byte before loop detach and the whole disk digest was unchanged
(`407d03b6fbbd7ce532f5cf7c87333e91268114342bd98b80a0be67afe0870637`). This qualifies Linux partition access and the
non-root recipe, not physical USB. On an ordinary temporary tree at umask 0022, plain `cp -r` retained 0555/0444
while `cp -r --no-preserve=mode` produced 0755/0644 with matching contents and working create/delete in the copied
directory.

## Matched lookup/copy measurements

The merged review found lookup reading each sibling inode before comparing names; name comparison now precedes
unrelated inode reads (lookup still scans directory records for duplicates; opendir still validates all listed
children and keeps its sorted snapshots and continuation cookies). A pool imported 4,175 regular files from
`/usr/share/man/man3` (6,406,415 bytes; 6,182 symlinks excluded), with the source, a 128 MiB pool (1 MiB EMPTY
journal), copies and results on tmpfs, both revisions built `-O2 -g3` with the same libfuse, user and GNU cp 9.10.
Each sample used a new FUSE instance and fresh destination: `npfs-fuse pool.raw mount`, then
`/usr/bin/time … cp -r --no-preserve=mode mount/man3 copy-N`, `diff -r source-man3 copy-N`, `fusermount3 -u mount` and a
pool digest check.

| Revision | Wall time samples (s) | Median (s) | cp peak RSS samples (KiB) |
| --- | --- | ---: | --- |
| `77a8c33` | 21.13 / 20.02 / 19.77 | 20.02 | 2660 / 2656 / 2544 |
| `375b569` | 1.72 / 1.70 / 1.73 | 1.72 | 2832 / 2684 / 2544 |

Every comparison and digest check passed; the median difference is about 11.6 times on this workload. Timings cover cp
only (not daemon startup, formatting, diff or hashes), RSS excludes the daemon, caches were not dropped and the Linux
VM was not isolated from other work. No profiling or physical-media measurement was made, and the EMPTY-journal
workload does not measure replay startup, overlay memory or worst-case lookup.

## Scope

Independent review found no correctness blocker (its minor feature-admission wording finding was corrected). The shared
loader validates the full staged payload before publishing an overlay or writing homes; writable fsck keeps
exclusive/writable admission, sequence exhaustion and durable home/EMPTY ordering; RAM replay preserves the source
control and sequence and uses read-only admission. Invalid payloads, allocation failures and extreme sizes were
source-reviewed, not fault-injected. The mount checks traversed metadata, global ownership checking remains fsck's
job, and the source must stay immutable throughout. The owner later reported successful physical stick mounting and
copying on Arch Linux, with no additional physical timing or digest measurements.
