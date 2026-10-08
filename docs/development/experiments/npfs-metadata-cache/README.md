# npfs metadata cache qualification

The metadata-read caching item from [review #348](https://git.internal/PyxisOS/pyxis-os/pulls/348)
was compared against main `b352a1d`, using cache implementation `ce1feaa`.
The I/O batching and namespace changes are absent from both measured kernels.
Before the runs, expected behavior was fewer backing reads for repeated metadata
access while preserving transaction overlays, namespace visibility, file bytes
and durability. The [cache reference](../../../devices/npfs-metadata-cache.md)
describes its bounds, invalidation and pressure behavior.

## Configuration and commands

Linux nested KVM; QEMU 10.2.2 with upstream AHCI fix
`d9f78431d8ebdc2d03ad74461138c1c9eb076aa5`; four CPUs, 256 MiB, `-cpu max`;
fresh matching OVMF CODE/VARS from `/usr/share/edk2/ovmf` per boot. CPU 3 ran
the existing remote session; CPUs 1 and 2 were idle. Profiling was off.
These are emulated-device elapsed measurements, not owner-host or physical-media
results. No concurrent benchmark QEMU process was running.

Both boots started from separate copies of one 132 MiB GPT disk under
`/dev/shm/pyxis-npfs-performance`, with partition 1 at sector 2049, 262144
512-byte sectors and GUID `12345678-1234-4567-89ab-0123456789ab`. Its 128 MiB
npfs pool had an 8 MiB journal and volume `bench`, importing only the installed
1 MiB `iobench.bin` fixture. `mkfs.npfs --size 128MiB --journal 8MiB --volume
bench --source /dev/shm/pyxis-npfs-performance/source` created the regular pool
file, initial `fsck.npfs` passed, and `sgdisk`/`dd bs=512 seek=2049` assembled
that disposable disk. No host block device was used.

Pyxis GCC 16.2.0 built the full baseline with `make -j16 image fs-tools`.
Pins were fs `d352c7e`, userland `ad1d53a`, ports `bf7667c`, lwIP `a1aadb9`.
The changed image rebuilt its kernel and reused those exact verified SDK,
userland and ports outputs with `PREBUILT="sdk userspace ports"`. Baseline
vendor port warnings were unchanged; the modified kernel build had no warnings.
No dependency or compiler-container change was needed.

Both builds used the existing host CMake environment and these overrides:

```sh
PATH=$HOME/src/pyxis-native-writer-build/host-tools/bin:$PATH \
make -j16 image PREBUILT="sdk userspace ports" \
  CROSS_COMPILE=$HOME/opt/pyxis-cross/bin/x86_64-unknown-pyxis- \
  PYTHON=/usr/bin/python3 \
  INIT=$HOME/src/pyxis-npfs-performance-build/npfs-performance/init-native.sh \
  INIT_PRIMARY=app://init-idle INIT_CPUS=3=app://init \
  MOUNT_DISK=12345678-1234-4567-89ab-0123456789ab
```

The uncommitted init mounted `--partition 1 --volume bench --read-write data://`,
created a namespace, started `textfs.pxe`, then launched `session.pxe
--configure-network --start-remote-services`. QEMU invocation (baseline
substituted `baseline` for `metadata`):

```sh
/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64 \
  -machine q35 -accel kvm -cpu max -smp 4 -m 256M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=/dev/shm/pyxis-npfs-performance/metadata-vars.fd \
  -cdrom /dev/shm/pyxis-npfs-performance/metadata.iso \
  -drive if=none,id=target,format=raw,file=/dev/shm/pyxis-npfs-performance/metadata-disk.raw,cache=writeback \
  -device virtio-blk-pci,drive=target,disable-legacy=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:23389-:2323 \
  -device virtio-net-pci,netdev=net0,disable-legacy=on \
  -object rng-random,id=rng0,filename=/dev/urandom \
  -device virtio-rng-pci,rng=rng0,disable-legacy=on \
  -display none \
  -serial file:$HOME/src/pyxis-npfs-performance-build/npfs-performance/metadata-serial.txt \
  -monitor stdio -gdb tcp:127.0.0.1:12389
```

Each guest command was submitted individually through `build/tools/pyxis-remote
--machine --no-shell-echo --columns 120 --rows 40 127.0.0.1 23389`, awaiting
completion before the next:

```text
iobench write data://grow.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared.bin --prepared --buffer 4080 --rounds 5 --sync
iobench read data://iobench.bin --buffer 4088 --rounds 5
```

The cache boot additionally ran:

```text
ls data://
date > data://coherent.txt
mv data://coherent.txt data://renamed.txt
cat data://renamed.txt
rm data://renamed.txt
cat app://share/iobench.bin > data://reused.bin
sync data://
exit
```

All succeeded, including lookup of the renamed contents and reuse after removal.
The baseline's extra small creates happened after the matched read command.
Complete decoded records are [the shared baseline record](../npfs-io-runs/baseline.txt) and
[metadata.txt](metadata.txt).

## Results and limits

Every benchmark verified one untimed warmup and five measured 1 MiB samples;
all commands exited zero. There were no short reads/writes. Readback/EOF checks
matched the fixture. Medians/ranges describe warm elapsed intervals, not cold
storage, per-call distributions or confidence estimates.

| Interval | Main median (range), ms | Cache median (range), ms |
| --- | ---: | ---: |
| Read open | 16.242 (9.002–16.281) | 0.755 (0.742–0.904) |
| Read payload | 165.911 (160.385–338.112) | 163.220 (159.579–173.033) |
| Complete read | 179.736 (177.215–354.913) | 164.462 (160.980–174.480) |
| Grow transfer | 197.462 (193.757–207.983) | 206.420 (202.185–211.008) |
| Grow sync | 197.152 (196.167–198.450) | 198.134 (196.912–198.276) |
| Prepared overwrite transfer | 153.359 (149.764–320.124) | 148.505 (143.886–150.982) |
| Prepared overwrite sync | 188.077 (62.798–191.799) | 186.299 (184.990–186.708) |

The improvement is in warm opens; cached file-payload work and serial data writes
remain. The 30-second background flush is still enabled and may overlap samples.
Main's outlying prepared transfer/sync pair and first measured read are retained,
not discarded. No general speedup is inferred for working sets exceeding the cache.

The remote client exited with complete draining; QEMU was quit. Stopped partition
extraction used `dd bs=512 skip=2049 count=262144`, and host `fsck.npfs --image`
passed. Extracted `grow.bin`, `prepared.bin` and `reused.bin` each matched the
installed fixture with `cmp`; baseline grow/prepared comparisons also passed.

This exercises ordinary directory mutation and reuse of an inode slot, not a
forced physical metadata-to-data block reuse, cache saturation, allocator failure
or pressure eviction. Those paths and failure/abort coherence have source-review
coverage only. No new tests, self-tests, fault injection or automation were added.
Physical-media and uncertain-I/O qualification remain separate.
