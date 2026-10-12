# FAT32 native mount baseline

Captured 2026-10-12 before implementation: Pyxis main `173f132141e0`, fs
`28386cc79ed3`, userland `5fa4f42291a9`, ports `28b5a3bce9aa` and lwIP
`a1aadb91a503`. Task 3 remains a proposal; no native FAT mount exists yet.

Fresh host tools: `make -C fs -j16 BUILD="$PWD/build/fs-tools"`. Fresh kernel,
userland and default image: `make -j16 image PREBUILT="sdk ports"
BOOT_MENU_TIMEOUT=5`, using the existing `pyxis-llvm23.1.3-49e2c1a` builder.
Reused SDK/ports bundles pass `scripts/bundle.sh verify sdk` and `verify ports`
against these inputs. No toolchain/container rebuild or new benchmark tool.

## Host shared reader

Use the original disposable image from the [raw-write record](fat32-raw-writes.md):
70 MiB GPT disk, entry 1 at sector 2048, 64 MiB FAT32, 512-byte sectors/clusters,
two FATs, 129022 clusters. It contains two files, three directories and five
allocated clusters. Run each existing CLI operation seven times on cached media,
with no image build running; Python `time.perf_counter` measures each complete
subprocess including startup and captured output:

```sh
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 check
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 list /
build/fs-tools/fat32-inspect --image base.img --gpt-partition 1 read /DIR/CHILD/HELLO.TXT
```

| Operation | Median ms | Min–max ms |
| --- | ---: | ---: |
| Whole-volume check | 1.467 | 1.335–1.626 |
| Root list | 0.474 | 0.455–1.017 |
| Small file read | 0.556 | 0.499–0.904 |

These unprofiled Linux VM measurements include process overhead; they are not
native adapter timings or physical-storage performance.

## Existing native npfs path

Disposable 134 MiB GPT disk, disk GUID `12345678-1234-4567-89ab-0123456789ab`:
copy the same FAT partition into entry 1 (first sector 2048, 131072 sectors);
entry 2 is a 64 MiB npfs pool (first 133120, 131072 sectors). Format the pool with
`mkfs.npfs --image pool.raw --size 64MiB --journal 1MiB --volume system
--source SOURCE --volume home`, then validate with `fsck.npfs --image pool.raw`.
SOURCE contains the fresh image's 32768-byte `share/iobench-small.bin` fixture
at its root and `config/boot.lua` selecting a shell with read-only system/home
roots, start `system`, launch enabled, network/power/screenshot disabled.

Interactive QEMU 10.2.2: q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF,
VirtIO SCSI CD, modern VirtIO block (`disable-legacy=on`), unused audio disabled.
Boot archive init with `init=boot://boot-init.pxe mount.disk=DISK_GUID`.
Attach the disposable disk writable: current boot-init opens system RW before
reading its pool configuration, although that configuration delegates only RO
roots here. The full disk SHA-256 is unchanged after the workload and shell exit.

```text
iobench read system://iobench-small.bin --bytes 32768 --rounds 5
```

Default 4088-byte requests, one untimed warm-up, nine payload reads plus EOF per
round; all five rounds verify 32768 bytes with zero failed passes. Profiling is
off and GDB detached during timing; clock-call overhead is not subtracted.

| Phase | Median ms | Min–max ms |
| --- | ---: | ---: |
| Open | 5.439 | 1.682–16.976 |
| Payload reads | 42.611 | 32.868–50.855 |
| Complete pass | 52.716 | 48.179–75.426 |

GDB before/after shell exit observes the same retained npfs pool at entry 2,
wrapper count 3 → 1, and empty retired-node/raw-claim lists. This confirms pool
retention, not a measured raw-claim refusal. Variation is broad in this nested VM;
repeat the matched workload after implementation. Raw captures remain local;
QEMU/debugger jobs are stopped. No native hardware qualification is claimed.
