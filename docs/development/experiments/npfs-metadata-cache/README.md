# npfs metadata cache qualification

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

The metadata-read caching item from [review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348) was compared against main
`b352a1d` using cache implementation `ce1feaa`, with the I/O batching and namespace changes absent from both measured kernels.
Expected before the runs: fewer backing reads for repeated metadata access while preserving transaction overlays, namespace
visibility, file bytes and durability. The [cache reference](../../../devices/npfs-metadata-cache.md) describes its bounds,
invalidation and pressure behavior.

## Configuration and commands

Setup, disk creation and build overrides follow the [shared baseline record](../npfs-io-runs/README.md#configuration-and-commands):
Linux nested KVM, QEMU 10.2.2 with the AHCI fix `d9f78431d8eb`, four CPUs, 256 MiB, `-cpu max`, fresh OVMF CODE/VARS per boot, CPU 3
running the remote session with CPUs 1 and 2 idle, profiling off and no concurrent benchmark QEMU. These are emulated-device
elapsed measurements, not owner-host or physical-media results. Both boots started from separate copies of one 132 MiB GPT disk
(partition 1 at sector 2049, 262144 512-byte sectors, GUID `12345678-1234-4567-89ab-0123456789ab`) holding a 128 MiB pool with an
8 MiB journal and volume `bench` that imported only the installed 1 MiB `iobench.bin`, made with `mkfs.npfs --size 128MiB --journal
8MiB --volume bench --source SOURCE`, `fsck.npfs` and `sgdisk`/`dd bs=512 seek=2049`; no host block device was used.

Pyxis GCC 16.2.0 built the baseline with `make -j16 image fs-tools` (pins fs `d352c7e`, userland `ad1d53a`, ports `bf7667c`, lwIP
`a1aadb9`) and the changed image rebuilt its kernel against the verified SDK, userland and ports outputs
(`PREBUILT="sdk userspace ports"`), with unchanged baseline vendor warnings, none in the modified kernel, and no dependency or
container change. The uncommitted init mounted `--partition 1 --volume bench --read-write data://`, created a namespace, started
`textfs.pxe` and launched `session.pxe --configure-network --start-remote-services`. Each command went through the machine-mode
remote client, awaiting completion:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
iobench read data://iobench.bin --buffer 4088 --rounds 5
```

The cache boot additionally ran `ls data://`, `date > data://coherent.txt`, a `mv` to `renamed.txt`, `cat`, `rm`, a fixture copy to
`data://reused.bin`, `sync data://` and `exit`; all succeeded, including lookup of the renamed contents and reuse after removal. The
baseline's extra small creates happened after the matched read command.

## Results and limits

Every benchmark verified one untimed warmup and five measured 1 MiB samples; all commands exited zero, with no short reads or writes
and readback/EOF matching the fixture. Medians and ranges are warm elapsed intervals, not cold-storage figures, per-call
distributions or confidence estimates.

| Interval | Main median (range), ms | Cache median (range), ms |
| --- | ---: | ---: |
| Read open | 16.242 (9.002–16.281) | 0.755 (0.742–0.904) |
| Read payload | 165.911 (160.385–338.112) | 163.220 (159.579–173.033) |
| Complete read | 179.736 (177.215–354.913) | 164.462 (160.980–174.480) |
| Grow transfer | 197.462 (193.757–207.983) | 206.420 (202.185–211.008) |
| Grow sync | 197.152 (196.167–198.450) | 198.134 (196.912–198.276) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 148.505 (143.886–150.982) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 186.299 (184.990–186.708) |

The improvement is in warm opens; cached file-payload work and serial data writes remain. The 30-second background flush may overlap
samples, and main's outlying prepared pair and first measured read are kept, not discarded. No general speedup is inferred for
working sets exceeding the cache. After clean exit, the stopped partition (`dd bs=512 skip=2049 count=262144`) passed host
`fsck.npfs --image`, and extracted `grow.bin`, `prepared.bin` and `reused.bin` each matched the installed fixture with `cmp`
(baseline grow and prepared comparisons also passed).

This exercises ordinary directory mutation and reuse of an inode slot, not a forced physical metadata-to-data block reuse, cache
saturation, allocator failure or pressure eviction; those paths and failure/abort coherence have source review only. No tests,
fault injection or automation were added, and physical-media and uncertain-I/O qualification remain separate.
