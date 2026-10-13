# FAT32 trusted-workflow baseline

Captured 2026-10-13 before task 5 code, after the owner cleared timing work:
Pyxis main `d3bc9b5aaa14`, fs `414be0ac0b06`, userland `c472de7d5d9a`, ports
`f93becb9e545`, lwIP `a1aadb91a503`. Fresh `make -j8 image BOOT_MENU_TIMEOUT=5`
uses the repository-pinned `pyxis-llvm23.1.3-4c23a20` builder. The earlier proposal control was refreshed
after main adopted this builder, before mutation code. Host tools build with
`make -C fs -j8 BUILD="$PWD/build/fs-tools"`. Preserve control artifacts for
matched/interleaved comparison after integration; no new benchmark infrastructure.

## Existing paths

Use the [disposable fixtures](fat32-mount-baseline.md): 70 MiB GPT/FAT disk with
entry 1 at sector 2048, 64 MiB FAT32, 512-byte sectors/clusters, two mirrored FATs,
129022 clusters, two files/three directories. Cached host operations, seven runs
each with no build running, use the same `fat32-inspect` commands and Python
`time.perf_counter` subprocess timing as that record:

| Host operation | Median ms | Min–max ms |
| --- | ---: | ---: |
| Whole-volume check | 1.553 | 1.402–3.531 |
| Root list | 0.518 | 0.477–1.000 |
| Small file read | 0.500 | 0.477–0.558 |

For the existing native npfs path, recreate the disjoint 134 MiB fixture using
current `mkfs.npfs` and the fresh 32768-byte `share/iobench-small.bin`. Keep the
same two RO shell roots/configuration and locally recorded boot disk selector
as the linked record. Disk identifiers are never copied into repository records.
QEMU 10.2.2, q35/KVM, host CPU, four CPUs, 1 GiB RAM, OVMF, modern VirtIO block,
VirtIO SCSI CD, unused audio disabled. Run interactively with profiling off and
GDB detached:

```text
iobench read system://iobench-small.bin --bytes 32768 --rounds 5
```

Default 4088-byte requests, one warm-up, five verified samples, zero failures.
Clock-call overhead is not subtracted. Nested-VM elapsed times:

| Phase | Median ms | Min–max ms |
| --- | ---: | ---: |
| Open | 0.436 | 0.372–0.564 |
| Payload read | 3.738 | 3.553–5.122 |
| Complete pass | 5.077 | 4.768–6.543 |

The npfs partition is byte-identical to the freshly formatted source after shell
exit. GDB PMM allocation snapshots are 4616 frames with the shell, 4251 after exit
(4096-byte frames). FAT instance/workspace counts remain zero. These are system
allocated-frame counts, including kernel and caches, not private session RSS.

## Existing FAT backing and functional controls

The earlier proposal control at main `2f2109fc41d5` used the same four-CPU
VirtIO setup and existing explicit scratch RW bootstrap
(`init=boot://init-fat32-mount-write.pxe boot.install=1`, disk 1/entry 1) holds one
published RW instance, one data node/control and 2706908 workspace bytes. PMM
allocated frames: 4479 before selection, 5566 with the admitted view, 4599 after
`close`, successful `retire`, `quit`. FAT instance/workspace/retired-node counts
return to zero; the disk hash is unchanged. These snapshots include other
allocation/cache changes and are not transient peaks or an isolated mount cost.

Functional controls also run the existing RO bootstrap on VirtIO (`ls`, known
file read, close/retire) and the RW bootstrap on disposable single-LUN USB BOT
through QEMU xHCI. USB create/open/write/sync/close/retire succeeds; independent
`mtype` reads the new file and unchanged original, and `fsck.fat -n` is clean.
These earlier functional runs contain no timing samples. Existing bootstraps do
not qualify the proposed configured/editor workflow or physical hardware.

QEMU/GDB jobs are stopped; raw logs, images and screenshots stay local. No real
ESP operation, forced unplug, repair or power-loss qualification is claimed.
