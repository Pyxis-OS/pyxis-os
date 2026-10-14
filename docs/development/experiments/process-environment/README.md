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

## Implementation qualification, 2026-10-14

Built code revision `c9f25f5f64919e84fc760da5a971f4e8d079222e`, rebased on
main `97a5c556a7ec`; userland `6f9276aa034b`, ports `2c49e86c8989`, unchanged
fs/lwIP. Complete ordinary image build passed with the existing e0801c2 builder.
Fresh 1-CPU then 4-CPU boots used the baseline configuration and exact commands.
All 12 measured connections exited 0 with complete FINAL drain; every IPC pass
and every 1 MiB transfer verified. No build, debugger or profiler overlapped
sampling. Host contention was not controlled; this is a separated nested before/
after, not interleaved native evidence. Retain all samples.

Raw captures, screenshots, GDB record and exact images:
`/shared/present/capability-environment/implementation-c9f25f5f/`;
SHA256SUMS hash `4d510f3cb6c74482c1e4b984f55c34f34fb6f71d3d7c622c2f8dc0208c174719`.
ELF `a916976e551b55c3ea44c0de60b3d3f29080e9fbe168555b156fbc26b6c59cb4`,
initrd `d31fb99a465d168a3812c02946c55f74718d77056a4e113d8b63f314f0c7f5c3`,
ISO `5b123ff098aac8a8216037f64a5cd478c82387dff34b2df4b1ae5ab2007b3d77`.

| IPC workload, ms: median (range), 100 verified passes | 1 CPU | 4 CPUs |
| --- | --- | --- |
| CALL64 completion | 84.743 (83.838–89.477) | 100.921 (70.572–126.360) |
| CALL4096 completion | 85.745 (84.067–123.942) | 102.355 (75.209–141.229) |
| SEND64 completion | 11.989 (11.651–23.509) | 16.130 (11.778–23.644) |
| SEND64 admission | 1.113 (1.085–2.043) | 1.227 (1.195–2.416) |
| SEND4096 completion | 13.202 (12.918–14.036) | 15.756 (11.904–28.856) |
| SEND4096 admission | 1.264 (1.238–1.469) | 1.315 (1.266–2.577) |

| OPEN/launch/pipe, µs: median (range), 10 samples | 1 CPU | 4 CPUs |
| --- | --- | --- |
| Native descriptor OPEN | 37 (36–40) | 44 (39–138) |
| Lua boot FILE creation | 35.870 (35.660–40.380) | 40.910 (40.020–47.190) |
| Lua text FILE creation | 2061.465 (974.650–2946.930) | 1584.265 (696.520–2788.320) |
| Echo launch through exit | 8080.800 (2764.620–8916.800) | 10219.985 (2430.600–16824.960) |
| Pipe acceptance, verified 1 MiB | 1404 (1389–1440) | 1060 (278–3434) |
| Pipe completion, verified 1 MiB | 1776 (1764–1845) | 1845 (764–3752) |

Lua clock reference was 34,734.6 / 36,991.77 ns per call (1/4 CPUs), not subtracted.
The slower launch medians and 4-CPU CALL4096 median remain observations with
broad overlapping ranges, not a native attribution. Source inspection identifies
new environment allocation/capture/lookup work on launch/OPEN; ordinary IPC has
no new environment lookup per message. Startup mapped bytes now exclude kernel
heap table/cells, so a smaller startup mapping cannot establish lower total memory.
The owner's matched native after-A/C runs remain due.

Interactive 1/4-CPU checks passed CAP alias lookup/removal, service pair collision
refusal, replacement through an already-held slot and use after publication/
controller removal. Four CPUs also passed exported TEXT inheritance into Lua.
The jobs-enabled local Multiplexer passed an explicit child TEXT override and
unchanged parent value, with captured output and confirmed job close. Remote's
unconfigured job profile honestly returned UNAVAILABLE; a mistaken standalone
`bin://lua.pxe` image returned NOT_FOUND. The successful native job used the
held content path `bin://lua.pxb/app/bin/lua.pxe`, without bundle expansion.

Existing `threadprobe runtime-io` reported PASS on both configurations, including
shared/dup FILE operations, positioned data, CLOSE/reuse and EOF/EPIPE. Its blocked
CALL checkpoints were not inspected in this run. Local and remote mux split,
numeric focus, pane teardown, reconnect and disconnect worked; Neovim opened and
quit in a remote mux pane. GDB inspected environment operations on both CPU counts:
after removals the shell returned to 39 entries/355 name-text bytes, with unlocked
table guard, zero VM borrowers/reserved capability slots, one logical owner of
the inspected cell and an empty completed queue. No injected calls or user-data
writes. These are selected observations, not instruction-level concurrency proofs.

Source review covered SET/UNSET/snapshot exclusion and lifetime; concurrent
mutation versus snapshot and policy-exhaustion races were not runtime-stressed.
First boot exposed cleanup exceeding the 16-mutation admission policy; exact
per-publication cleanup fixed it. Equal-generation TEXT readers now reuse their
cache publication, preserving borrowed pointers without mutation. No new tests,
boot automation or klog lines. All own QEMU/GDB/client jobs stopped.

## Remaining dependency

All three owner decisions are accepted. Userland #261 and ports #116 are published;
LLVM #9 at `df87791031b9b81060d99b7fb3c797aae6e68efb` object-compiles all five
affected target units against this ABI. Full hosted linking/runtime qualification
is pending the owner-published matching builder and a fresh immutable payload.
The registry currently reports manifest unknown for the df87791 tag. Owner build
branch `toolchain/process-environment-container` at `7fe4e887` prepares pin/tag
adoption; production provenance matching remains unchanged. Task 2 stays unchecked
and #832 stays draft until that dependency and remaining qualification are complete.
