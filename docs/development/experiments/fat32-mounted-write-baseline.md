# Mounted FAT32 write baseline

Captured 2026-10-12 before task 4 mutation code: main `36bcd7e96d3a`, fs
`30f625a236dd`, userland `3daa24611e1e`, ports `fd1eb30f17f3` and lwIP
`a1aadb91a503`. Task 3 is merged; mounted FAT is still read-only.

Build fresh host tools with `make -C fs -j16 BUILD="$PWD/build/fs-tools"` and
SDK/kernel/userland/ports/default image with `make -j16 image
BOOT_MENU_TIMEOUT=5` in the existing published
`pyxis-llvm23.1.3-2d23443` builder. Both builds succeed. No compiler rebuild,
benchmark infrastructure, fault hook or klog change. Preserve these binaries and
images for interleaved comparison after task 4.

## Existing shared reader and raw writer

Copy the [original disposable fixture](fat32-mount-baseline.md#host-shared-reader):
70 MiB GPT disk, entry 1 at sector 2048, 64 MiB FAT32, 512-byte sectors/clusters,
two FATs, 129022 clusters, two files and three directories. Run each existing
reader operation seven times on cached media with no build running:

```sh
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 check
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 list /
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 read /DIR/CHILD/HELLO.TXT
```

Python `time.perf_counter` measures complete subprocesses, including startup and
captured output. Linux VM elapsed milliseconds:

| Operation | Median | Min–max |
| --- | ---: | ---: |
| Whole-volume check | 1.765 | 1.387–2.876 |
| Root list | 0.511 | 0.481–1.004 |
| Small file read | 0.479 | 0.467–0.860 |
| Raw create/append/extend/replace/check session | 49.849 | 48.618–275.778 |

For each of the seven raw sessions, copy the original image before timing and
run `fat32-write --image COPY --gpt-partition 1` with this stdin:

```text
create /BASE.TXT = abc
append 0 = def
extend 0 65536
replace 0 = after
check
quit
```

Every measured session succeeds, issues receipt 0 and checks the final volume.
The first measured session is the 275.778 ms outlier; keep it in the range.
These startup/fsync-inclusive
host costs are neither mounted mutation timings nor physical-media throughput.

## Existing native paths

QEMU 10.2.2, q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF, modern VirtIO
block (`disable-legacy=on`), VirtIO SCSI CD and unused audio disabled. Use copied
scratch media and private ISO configurations, not the owner's ESP. Manual console
interaction and GDB inspect the existing paths; GDB is detached during timing.

The [134 MiB GPT fixture](fat32-mount-baseline.md#existing-native-npfs-path)
has a 64 MiB FAT partition at sector 2048 and a disjoint 64 MiB npfs partition
at sector 133120. Boot the fresh default archive with
`init=boot://init-fat32-mount.pxe boot.install=1`; attach scratch media read-only.
Explicit disk 1 / GPT entry 1 admission, `ls /` and
`cat /dir/child/hello.txt` succeed and match the host reader. GDB observes one
instance, 24416 workspace bytes, three wrappers and one retained data node while
the browser holds its root. Drained `quit` leaves zero FAT instances, workspace,
wrappers and extents. These are accounted-backing snapshots, not a total resident
memory or transient peak measurement.

Recreate the disjoint npfs fixture using current host tools and the fresh
32768-byte `share/iobench-small.bin`; its configuration delegates read-only
system/home roots to the shell. Boot with
`init=boot://boot-init.pxe mount.disk=<DISK_GUID>`.
Attach writable because current boot-init opens the pool RW before reading its
configuration. Run the existing command, profiling off:

```text
iobench read system://iobench-small.bin --bytes 32768 --rounds 5
```

Default 4088-byte requests, one warm-up, five verified passes, zero failures;
clock-call overhead is not subtracted. Reported elapsed milliseconds:

| Phase | Median | Min–max |
| --- | ---: | ---: |
| Open | 0.505 | 0.398–1.528 |
| Payload reads | 4.099 | 3.931–5.424 |
| Complete pass | 6.324 | 5.187–8.490 |

After shell exit, npfs retains its pool and one wrapper as before; FAT accounting
remains zero. The full scratch disk SHA-256 is unchanged across the npfs run.
QEMU/GDB jobs are stopped. Raw logs, images and screenshots stay local.

No mounted write, interval, failed-state recovery, dirty marking, resize or clean
RW retirement exists at this revision. Their qualification follows implementation;
these baseline runs establish only the existing paths and backing. Nested-VM
results do not qualify native ESP, USB/NVMe, power loss or torn-sector durability.
