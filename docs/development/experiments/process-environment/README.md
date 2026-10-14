# Process environment: task 2 record

## Baseline, 2026-10-14

Captured before implementation. Built revision
`b26a19b2c98bdcb825fa3f777b99ddb662b0965e` differs from main `30fb8888`
only in milestone documentation. Userland `5cd789a737f6`, ports `2296a06c3ff9`,
fs `d84f8084cb7f`, lwIP `a1aadb91a503`; all source inputs clean.
Ordinary `make -j16 image` passed in the existing
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-e0801c2` container.
Bundle manifests say `builder_image=local`; the actual invocation used that
image, with no compiler rebuild, probe or new klog lines.

Nested KVM Q35, standard VGA, VirtIO-net/rng, OVMF, 2 GiB, fresh 4-CPU boot
then fresh 1-CPU boot. Stock live/Remote configuration; localhost TCP 24084
forwarded to guest 2323. No own build, GDB or profiling overlapped sampling;
host contention was not controlled. One invocation per workload/configuration,
not an interleaved native A/B or a performance conclusion. Keep every slow
successful sample. The [owner's native baseline](../capability-model-baseline.md)
remains separate; its after-A/C repeats remain due.

Exact artifacts and raw JSONL/build log:
`/shared/present/capability-environment/baseline-b26a19b2/`.
Its `SHA256SUMS` hash is
`ae7c9caabde768f34a6718678665efddef6c575a0abfaf1f270e1e4df0c0150d`.

| Artifact | SHA-256 |
| --- | --- |
| caelum.elf | `0671db9dc4715819fc0d4e04117a4380ca6ae9fdf90443b2edf03562fa65039a` |
| initrd.cpio | `5574066d08b58a4f3f3b507c8788213f6978998dbd7d48da2ff30bf29fe47aa8` |
| pyxis.iso | `67d8de8a84de8e217299c4c10a2838334337945b7c9984d17eae8a37c4ada5a5` |

Launch the already built image with existing commands:

```sh
make run PREBUILT='kernel sdk userspace ports' ACCEL=kvm CPUS=4 MEMORY=2G \
  QEMU_DISPLAY=none VIRTIO_NET=1 TCP_FORWARD=24084:2323 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd
```

Repeat with `CPUS=1`. Use the existing host client
`pyxis-remote --machine --no-shell-echo 127.0.0.1 24084`; each `session`
command needs a fresh connection and must end with status 0 and complete FINAL
drain. Manually entered guest commands:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 100
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 100
bin://iobench.pxe read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 10
session bin://iobench.pxe pipe --buffer 4096 --rounds 10
```

Lua FILE creation and echo-through-exit use the exact ten-sample commands in
the [baseline run sheet](../capability-model-baseline.md#timed-batch), including
untimed warmup, `cat text://welcome` and the separate 1,000-clock-call reference.
Each CPU's `open-launch.jsonl` has descriptor read, Lua boot OPEN, Lua text OPEN,
echo launch, clock reference, cat, exit as commands 1–7. Every measured command
exited 0; all 12 connections had complete FINAL drain. One client attempted to
connect before the 1-CPU boot was ready and got Connection refused; it produced
no timing sample. All own QEMU/client jobs stopped after capture. Startup
allocation was not measured locally; the native record already identifies its
role and full backed allocation rather than substituting a header size.

| IPC workload, ms: median (range), 100 verified passes | 1 CPU | 4 CPUs |
| --- | --- | --- |
| CALL64 completion | 84.222 (83.626–85.116) | 96.136 (75.004–121.764) |
| CALL4096 completion | 84.568 (83.650–338.085) | 77.697 (74.728–90.135) |
| SEND64 completion | 13.070 (12.804–13.637) | 15.040 (11.443–18.710) |
| SEND64 admission | 1.204 (1.189–1.716) | 1.212 (1.152–1.712) |
| SEND4096 completion | 13.192 (12.608–16.204) | 15.525 (11.439–21.703) |
| SEND4096 admission | 1.273 (1.215–1.946) | 1.284 (1.214–2.179) |

| OPEN/launch/pipe, µs: median (range), 10 samples | 1 CPU | 4 CPUs |
| --- | --- | --- |
| Native descriptor OPEN | 40 (40–43) | 40 (40–51) |
| Lua boot FILE creation | 38.485 (38.290–42.220) | 49.760 (37.090–163.640) |
| Lua text FILE creation | 2716.410 (1079.270–4361.570) | 1343.605 (979.680–3034.880) |
| Echo launch through exit | 5148.925 (1907.520–7185.110) | 8935.195 (2971.630–10683.380) |
| Pipe acceptance, verified 1 MiB | 1383 (1357–1518) | 1088 (240–5659) |
| Pipe completion, verified 1 MiB | 1751 (1742–1887) | 2432 (592–6131) |

Descriptor/pipe values retain the tools' rounded summaries; Lua values are
recomputed from raw integer nanoseconds. Clock reference, not subtracted:
Lua wrapper 36,094.7 / 37,681.69 ns per call on 1/4 CPUs; descriptor benchmark
36,208 / 35,665 ns per read; pipe 35,886 / 35,855 ns per read. IPC's own
clock references remain in each raw header. These large nested clock costs and
sample variation prevent fine-grained native cost attribution.

## Checkpoint

Both task-2 scope defaults and the stream-entry retirement profile are accepted
(owner, 2026-10-14), in the
[milestone](../../../wip/capability-model-implementation.md#accepted-lifetime-decision-standard-streams-owner-2026-10-14).
No task-2 qualification is claimed by this baseline. Follow with the matching kernel/userland/ports and
LLVM target-source migration, build and interactive 1/4-CPU QEMU/GDB checks.
