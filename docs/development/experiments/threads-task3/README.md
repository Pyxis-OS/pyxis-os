# Shared-table delivery qualification

Threads task 3, 2026-10-10. Owner accepted prospective grant capture with exact
source recheck and precommit REPLY policy validation/move delivery. Exactly one
user task remains; no public ABI change, new klog lines or new test infrastructure.
Implemented behavior is documented in [threads](../../../wip/threads.md#shared-capability-and-vm-ownership).
This record separates the frozen comparison from later integration and native evidence.

## Pre-code baseline

Clean main A `b0d0bc077b96f908a2041f69279e83bce5efd452`, after merged #671.
All four bundles from successful exact-head [#1747](https://git.internal/PyxisOS/pyxis-os/actions/runs/1747)
were verified, then ordinary `make -j16 image PREBUILT="kernel sdk userspace ports"
REMOTE_BEACON=t14` passed using the existing LLVM 23.1.3 builder `49e2c1a`.
No compiler rebuild or upstream fetch. Host client: `make -C tools remote`.
Inherited pins: userland `1162d7299dffdf2c183a7a2b15cd6dfc08724bec`, ports
`f510e21286dfc1f8ae7f0e5f8c209d31698356ed`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

| Frozen input | SHA-256 |
| --- | --- |
| A ELF | `49b751ecea39e8c284aa74e8c273a9849f16305c07ebe8e9b9c925ee9fe91927` |
| A ISO | `9a7f496139e587b8fdfa342dc517b191a17adf7e3a87593f1407a6ac228da947` |
| Shared staged initrd | `6210eb078ba9955b49402185020143765efef784b9d471b6639058cb890dd9da` |

QEMU 10.2.2, Q35/nested KVM, CPU host, one socket with 1 or 4 cores and one
thread/core, 2 GiB, UTC RTC, matching raw OVMF/fresh VARS, modern VirtIO
SCSI CD/RNG/net, standard VGA/display none. Info logs, profiling off; no debugger
or own build during timings. Reverse client with `--machine --no-shell-echo
--listen t14 --beacon-address 127.0.0.1 0.0.0.0 2323`, UDP NAT forward 2324;
GDB port 12673. Loopback beacon is only the documented NAT adaptation.
All baseline clients/QEMU stopped before code; raw captures in ignored
`build/task3-baseline/{1,4}-A1`. Nested-VM measurements are not native costs.

Each boot ran call/send at 64/4096 bytes, 256 messages, 100 rounds. All eight
invocations returned verified warmup, 100 verified passes, zero failed CALLs or
rejected SENDs, summaries and FINAL 0/complete: 800 timed passes total. Native
follow-up can use the same [hands-free loop](../../../userland/remote-terminal.md#consumers-and-limits)
with `--rounds 100`. Clock calibration is retained, never subtracted.

Launch: `session bin://lua.pxe -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0)
end; print('launches=1024')"`; 16-child warmup and three timed sessions per CPU,
host `/usr/bin/time -f %e` including reverse discovery, launch, output and cleanup.
The session grants allow launches without altering the staged Remote policy.
Page workload: `allocbench pages --size 65536 --live 1 --rounds 64`, three fresh
processes/CPU; 64 allocations/releases and zero failures each. This exercises
BSP allocation and scheduler notification; actual IPI path inspection is separate.

Initial A1 medians (range), milliseconds except launch seconds:

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| CALL64 complete | 87.044 (86.122..91.393) | 98.968 (75.310..123.024) |
| CALL4096 complete | 87.249 (86.368..99.328) | 95.763 (75.399..132.791) |
| SEND64 admit / complete | 1.189 (1.165..1.985) / 13.591 (12.737..14.212) | 1.199 (1.117..1.866) / 14.772 (11.226..23.509) |
| SEND4096 admit / complete | 1.252 (1.236..2.524) / 13.713 (12.930..16.943) | 1.262 (1.203..2.598) / 12.900 (11.475..25.019) |
| 1024 launches, seconds | 4.30 (3.36..4.33) | 4.41 (3.96..6.11) |
| Pages, ms | 5.251 (5.232..6.280) | 4.886 (4.868..5.018) |

## Frozen B and manual qualification

Clean code B `37e08f12` built the ordinary kernel/image warning-free with
`make -j16 image PREBUILT="sdk userspace ports" REMOTE_BEACON=t14`, verified A's
exact bundles and the same compiler. Effective configuration and the complete
staged initrd bytes are identical. B ELF SHA-256
`b04ef6ab920cd8b0f8a4e0f554cfd6055525082e6fc3dc3cf7017599ba016755`;
ISO `3f6499deae1dcb8c1c3addd9a65826e65fcf02928a68849ef28122766232310a`.
Later documentation-only commits do not change this frozen comparison.

Interactive QEMU/GDB used matching symbols, `set may-call-functions off` and no
target state injection, faults, saturation or new test infrastructure:

- **1 CPU:** full pipe warmup/sample each verified 1 MiB, zero errors. GDB saw a
  RECEIVE claim at index 3/generation 2 with object NULL/reserved=true, and the
  queue's receipt references=1. Moving it left references=1 and claims=0. CALL
  held four hidden reply claims; grantless collection cleared the record's token
  and table claims to zero. No transient duplicate receipt ownership.
- **Captured authority:** launch's pipe writer had two logical grants and three
  storage refs while the source generation/masks were rechecked under the guard.
  Capture returned CAP_OK with two grants/two refs. Natural source CLOSE then
  reduced grants 2→1 while writer_closed remained false; the child completed its
  verified pipe transfer.
- **4 CPUs:** SEND4096 warmup/pass verified eight messages. Opening/closing 80
  existing boot files forced BSP growth 64→128; entries 0/63 and the first
  object's references survived replacement, while new slot 64 was empty,
  generation 1 and unreserved. Observed growth had zero live claims; preservation
  of claim markers during growth is inspected, not injected. GDB also observed
  CPU1 notifying BSP through the existing terminal-readiness rescheduling IPI.
- **Nonzero reply grant:** `cat text://welcome` succeeded. At collection, the
  reply owned one FILE export reference with READ/CALL authority and four claims.
  Move kept reference count 1, published the exact object/masks/generation,
  cleared the source grant/token and released all claims. The provider text
  reached the client with command status 0 and FINAL 0/complete.
- **Ordinary paths:** `echo ... | cat` exercised batch observers and pipe grants;
  RAM redirect/read/remove returned the expected bytes/status 0. Missing read and
  removal from the boot archive returned status 1; following shell exit drained
  successfully. Reverse sessions exercised UDP discovery and TCP delivery.

At this frozen-B qualification stage, no native post-change run, competing
sibling operation, reference saturation or forced policy rejection was claimed. HOST/NPFS CREATE and raw-disk/mount error
unwind were source-reviewed, without an attached backend in these boots.
Independent reviews found and corrected remaining preflight gaps in mount,
disk, namespace and console wrappers; no blocking finding remained.

Measured type sizes A→B: table 32→40, process 152→160, delivery record
8544→8624, HOST request 4992→5040, NPFS request 4920→4968 bytes. Entries remain
32 bytes (claim flag uses prior padding). The request catalog sizes its area from
the largest typed record; no fixed size limit was added. Launch's separate owned
grant/slot/handle storage uses 48 bytes per captured grant, preserving the
existing 64 KiB wire/string budget. Raw build/GDB captures remain in ignored
`build/task3-after`.

## Interleaved comparison

Each CPU configuration ran fresh A1/B1/A2/B2 boots. The pre-code A1 profiles
preceded implementation; remaining profiles used the frozen images, no debugger
or own builds during timings. All 32 IPC invocations returned 100 verified timed
passes plus warmup, zero failures/rejections and FINAL 0/complete: **3200 timed
passes**. All 24 measured launch sessions reported 1024, all eight warmups 16,
and all 24 page runs reported 64 allocations/releases with zero failures. No
complete sample was filtered. Raw JSONL/timings remain under
`build/task3-{baseline,after}/{1,4}-{A1,B1,A2,B2}`; decoded summaries/ranges and
clock calibration are in `build/task3-after/results.json`.

Median milliseconds, except whole launch session seconds; 100 IPC samples and
three launch/page samples per cell:

| 1 CPU | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 complete | 87.044 | 87.297 | 87.735 | 87.040 |
| CALL4096 complete | 87.249 | 87.267 | 90.563 | 87.257 |
| SEND64 admit / complete | 1.189 / 13.591 | 1.181 / 13.530 | 1.227 / 13.846 | 1.183 / 13.527 |
| SEND4096 admit / complete | 1.252 / 13.713 | 1.257 / 13.761 | 1.312 / 14.232 | 1.255 / 13.697 |
| 1024 launches, seconds | 4.30 | 4.31 | 3.27 | 4.26 |
| Pages | 5.251 | 5.333 | 5.242 | 5.262 |

| 4 CPUs | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 complete | 98.968 | 77.458 | 94.096 | 97.393 |
| CALL4096 complete | 95.763 | 77.785 | 93.336 | 99.644 |
| SEND64 admit / complete | 1.199 / 14.772 | 1.193 / 12.547 | 1.193 / 14.771 | 1.192 / 16.719 |
| SEND4096 admit / complete | 1.262 / 12.900 | 1.263 / 12.231 | 1.262 / 15.548 | 1.619 / 20.909 |
| 1024 launches, seconds | 4.41 | 4.23 | 3.73 | 3.49 |
| Pages | 4.886 | 4.767 | 4.932 | 5.022 |

One-CPU IPC medians are mostly stable. Four-CPU completion changes direction;
B2 SEND4096 admission is 28.3% higher and completion 34.5% higher than A2.
Those valid samples remain. B2 ranges include CALL4096 74.014–261.860 and
SEND4096 completion 11.651–160.171 ms; A2 CALL4096 reached 398.588 ms on one CPU.
Calibration across full profiles is 32,924–61,820 ns/read, never subtracted.
Launch wall time includes reverse discovery's one-second cadence and transport;
it is not isolated child-startup cost. These nested-VM results do not establish
zero overhead, a speedup or native percentages.

Raw SEND adds no new storage retain/allocation: source admission is the task-2
path. Delivery layout grows and initializes reply-claim metadata; RECEIVE and
DRAIN now use claimed/moved grants and change their lock/work pattern outside
the admission interval. Those can affect completion/scheduling, but no measured
cost share is assigned. A focused fresh four-CPU SEND4096 A3/B3/A4/B4 follow-up
retained all 400 verified samples and four successful FINAL records:

| Run | Admit median ms (range) | Complete median ms (range) | Clock ns/read |
| --- | --- | --- | --- |
| A3 | 1.256 (1.188..2.066) | 13.342 (11.362..23.031) | 36,631 |
| B3 | 1.261 (1.192..1.738) | 13.088 (11.438..20.788) | 37,275 |
| A4 | 1.259 (1.198..2.700) | 12.589 (11.490..24.691) | 37,202 |
| B4 | 1.265 (1.220..1.402) | 14.100 (11.474..22.340) | 35,502 |

The large B2 admission increase did not repeat: follow-up pair admission changes
are +0.4%/+0.5%, while completion changes direction. No sample is discarded and
no stable causal overhead is established from these nested runs. Native A/B was
then pending owner qualification; the native results below use the same initrd
and four-command remote loop at 256 messages/100 rounds. No optimization is included merely to improve these
nested results.


## Current-main integration

Merged main `2a1e4f99` into branch head `3aa47f1b`, retaining the frozen A/B
inputs above. Verified SDK/userland/ports bundles from successful exact-main
[#1755](https://git.internal/PyxisOS/pyxis-os/actions/runs/1755); inherited ports
pin is now `4047297dbc9342b8986867122d6e4c24ee6a57a2`, with the other pins unchanged.
Ordinary source kernel/image build with the same compiler and
`PREBUILT="sdk userspace ports"` passed warning-free. Integration ELF SHA-256
`9e5154c51e3f414151986e70ba0f0f8cf560e0cf56f8e75b3da02125e57313b5`,
ISO `abc80f1a25f08a98cb277daec2b4a5610ef602271e1801380b7684f5915e9af6`,
initrd `58ae5de4d3eb2a90e21cd9ddd61187d5b157eec247b2c6c40cf6134bd71647d0`.
These are integration inputs, not another performance comparison.

Fresh interactive QEMU boots used the same 1/4-CPU configuration. One CPU
verified CALL64 (eight messages, one round) and the 1 MiB pipe warmup/sample;
four CPUs verified SEND4096 (eight messages, one round), provider text, a
pipeline and RAM redirect/read/removal. Screenshot succeeded and a normal Lua
reader verified its eight-byte PNG signature before removal. Denied boot-archive
creation was followed by Not found, and removal from that archive was refused.
All sessions ended with FINAL 0/complete; expected refusals retained their own
nonzero command status. No kernel fault or panic. The initial attempt to list a
PNG as a directory was refused; it was replaced by the signature check above.

Independent source review confirmed main's frame-skipping integration: pending
capture forces composition, arrival after the snapshot remains pending for the
next pass, failures release claims, and success moves the snapshot grant once.
Prior matching-ELF GDB observations remain the inspection evidence. Integration
captures are in ignored `build/task3-integration`; all owned VM/client/debugger
jobs were stopped. No attached HOST/NPFS/raw-disk or native result is added.

For native A/B, keep the same staged initrd for A and B and run this on horse
once per fresh PXE boot, using a separate output directory for each run:

```sh
run_dir="$HOME/xf/task3/A1" # change for each fresh boot
mkdir -p "$run_dir"
for mode in call send; do
  for size in 64 4096; do
    printf 'session bin://ipcbench.pxe %s --size %s --messages 256 --rounds 100\n' "$mode" "$size" |
      pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 >"$run_dir/$mode-$size.jsonl"
  done
done
```

No sleep or guest `exit` follows the session command: stdin EOF closes the parked
shell only after its successor finishes, and the client drains through FINAL.
Check each JSONL for 100 verified passes, zero failure/rejection and FINAL status
0/drain complete; client exit alone is not the benchmark result.


## Native A/B reported by the owner

ThinkPad, 12 CPUs, AC, PXE, hands-free; fresh boots A1/B1/A2/B2. A is main
`2a1e4f99` (owner ELF prefix `72d1a0d1`), B is PR head `2a958d57` (luna build,
ELF prefix `1465bac0`). Main `2a1e4f99`'s initrd is identical across all runs.
Each boot ran the four commands above with 256 messages/100 rounds. Raw JSONL:
`/shared/present/batch2/ab684/684{A1,B1,A2,B2}/`. Local inspection verified all
16 files: **1600 successful verified timed passes**, zero CALL failures/SEND
rejections, and every FINAL status 0/drain complete. Hardware/build identity is
owner-reported; raw output was independently inspected. This adds IPC evidence,
not native launch/IPI measurements.

Owner-reported medians in milliseconds, 100 verified samples per cell:

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 complete | 1.240 | 1.245 | 1.230 | 1.215 |
| CALL4096 complete | 2.087 | 2.086 | 2.050 | 2.005 |
| SEND64 complete | 0.519 | 0.505 | 0.371 | 0.374 |
| SEND64 admit | 0.081 | 0.102 | 0.062 | 0.065 |
| SEND4096 complete | 1.151 | 1.257 | 1.118 | 1.244 |
| SEND4096 admit | 0.230 | 0.235 | 0.230 | 0.242 |

SEND4096 completion rises in both pairs: +0.106/+0.126 ms per 256 messages,
+9.2%/+11.3%, or about **0.41–0.49 µs/message**. Admission rises +2–5%.
CALL and SEND64 completion show no corresponding repeated increase. All raw
samples are retained; SEND4096 completion ranges A1/B1/A2/B2 are
0.416–1.752 / 0.413–2.008 / 0.411–1.747 / 0.409–1.747 ms. Clock calibration
is 136–307 ns/read across the 16 commands, never subtracted. Unlike the nested
repeat, this native paired result merits recording as a regression budget.

**Source attribution, not a profile.** The benchmark has no payload attachments.
A batch performs 256 raw SENDs and data RECEIVEs/FINISHes, plus 32 DRAIN control
CALLs (eight messages/group). SEND adds no reply slot claim or grant retain;
it initializes the new reservation metadata. RECEIVE claims one receipt slot,
checks its generation and moves the queue's initial receipt reference. DRAIN
CALLs claim four reply slots and release them during grantless collection;
REPLY policy validation sees zero grants. These are the relevant reservation
changes inside completion, not four slots or reply grants per raw SEND.

The RECEIVE path actually reduces table-guard acquisitions from three to two
and removes a receipt retain/release pair relative to A; it adds claim markers,
generation checks and reservation accounting. The payload copy operations and
userland code are unchanged, with no extra 4 KiB copy. Delivery records grow
8544→8624 bytes; matching-ELF offline GDB shows request/reply offsets moving
352/4448→432/4528. That changes buffer alignment and pool stride, so a cache/layout
effect is plausible, but unmeasured. Reservation work is size-independent;
the source does **not** establish which change causes the 4096-only increase,
or assign the whole delta to atomic installation.

**Recommendation:** accept the measured sub-0.5 µs/message completion budget
for this correctness milestone. No demonstrated cheap fix is identified.
Removing claims would violate the accepted delivery contract; rearranging buffers
or fusing helpers without profiling would be speculative. Keep the code unchanged
for merge and revisit this path if native profiling identifies a specific hot
operation or sustained workload makes the budget material. This recommends the
tradeoff; it does not mark owner acceptance of the regression as already given.
