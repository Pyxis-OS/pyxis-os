# Admitted call references and CLOSE qualification

Threads task 2, 2026-10-10. Exactly one user task per process remains. No public
ABI, SDK/compiler policy, dependency pin, thread creation or klog change.

## Change and inspected lifetime

`capability_acquire` holds the short IF=0 table guard while validating the exact
generation and required masks, capturing rights/transport and retaining object
storage. Failed admission clears the reference; saturation returns `CAP_LIMIT`.
Release clears the local reference before dropping storage. Dispatch, INFO/COPY,
readiness and secondary handles in endpoint, namespace, launcher, directory,
clipboard and startup preparation use this ownership. Blocking cleanup drains
references before `task_syscall_leave`; no table entry pointer crosses growth.

CLOSE clears the entry and advances/possibly retires its generation before
logical release. Its detached grant already owns storage, so CLOSE adds no
saturation failure. Table destruction detaches its array before applying the
same effects. Table guards span no callback, allocation, user access or wait.
Growth copies/publishes under the guard, but still requires exclusive ownership;
installations/reservations and BSP table loans are task 3, not made sibling-safe
by this change. Process/table teardown still relies on the sole task's completed
syscall unwind and returned loans.

| Logical close audit | Storage-reference behavior |
| --- | --- |
| Group controller, console interrupt and terminal authority counters | Existing grant retain/release remains distinct; an admitted operation adds no controller/stream authority. |
| Endpoint receiver/receipt | Detached CLOSE applies the existing locked transition immediately; admitted receipt storage cannot be recycled until operation cleanup. Export retirement still includes accepted work and its existing client references. |
| Pipe endpoints | Every capability/transfer grant, including zero-right copies, counts an open end. Plain CALL/readiness references do not. Final grant release closes/notifies independently of deferred BSP destruction; pair storage waits for both physical endpoints. |
| Other objects | Existing final-storage destruction/worker cleanup remains unchanged. No new revocation or concurrent-operation policy. |

Public admission sites map storage saturation to `CALL_LIMIT`. Private startup
validators retain their existing boolean/`MM_INVALID` failure boundary (launch
preparation refuses through its existing `CALL_BAD_REQUEST` path); no unrelated
memory-result/API expansion was introduced. This boundary was inspected, not
forced to saturate at runtime.

Metadata measured in the matching ELFs: capability table 24→32 bytes, process
144→152, pipe endpoint 56→64, pipe pair 65,696→65,720. Kernel stacks remain 16 KiB;
the shared request layout is unchanged. No new per-call heap allocation.

## Revisions, build and environment

Pre-code baseline A is clean main `a3fa67289f67f970ce1c720258d0cdf8436b2336`
(merged #662). All four bundles from successful exact-head workflow
[#1697](https://git.internal/PyxisOS/pyxis-os/actions/runs/1697) were verified
before ordinary image assembly. Frozen B is `2bd95a8887e049560db70511c5bb19736bfaa36c`,
built with `make -j16 image PREBUILT="sdk userspace ports"` in the existing
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a` container. Build passed
without warnings. The generated `.config` comment rewrite was inspected and
restored; effective values/config hash are identical. Firmware came from the
exact baseline cache after a network-disabled attempt refused an absent mirror
cache; no upstream fallback or compiler rebuild.

The timing images use A's exact staged initrd with `Remote launch=true` in its
generated `config/live.lua`, allowing group-bound Lua child launches. No tracked
configuration changed. Existing image assembly compared complete staged trees:
only `boot/caelum.elf` differs.

A/B both pin userland `6e12b5f4de26a8bd32755efb27bff24d32a9d147`, ports
`7c33f3ffe2a465e1ecef8783353b06c23c7664fa`, fs
`b427df29f865bc361b8da92bcd74e114581e9a32` and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

| Frozen component | SHA-256 |
| --- | --- |
| A ELF | `19b90bc639fafd84ffd2b90c9d571c25eb915793756934222a49dd1a61092e3f` |
| A ISO | `6aeb47e6d6c6bfeab491fe76253d26bd500eae40a7e2936716b284af816fa669` |
| B ELF | `3301806cfa09c65a22f44e192ba51a56864d9781d74df11131d65ac38f7e68ac` |
| B ISO | `32c23ec11c924dcba164f8f52abbf98c41325d99a04da734cf7b0045e81c02c7` |
| Shared initrd | `2a8eb4737f066d7e731767cc4c595045335b665193538d45585feea34fe397f1` |
| Effective kernel config | `ac12acc93c3fcbbdff1ace10d95b8f3cdf883e1c09d21ce413ba09df8324a98b` |

QEMU 10.2.2, Q35, nested KVM, `-cpu host`, 1 or 4 cores/one socket/one thread,
2 GiB, RTC UTC, matching raw OVMF CODE plus fresh writable VARS, modern VirtIO
SCSI CD/RNG/net, standard VGA with display none, NAT. No USB/HDA/TAP. Info logs,
allocation profiling off. Interleaved comparisons use ports 24671 (remote) and
12671 (GDB); separate functional inspection uses 24731/12731. No build or debugger
pauses during timings. Unrelated host jobs were preserved; this is shared-host
nested-VM evidence, not native performance or a guaranteed quiet host.

Main subsequently advanced to `d01fe599` with power-overlay/console-readiness
and libuv integration. The implementation rebased cleanly to `b4ec367c`; source
review preserved the new readiness paths. Frozen comparison revisions remain
unchanged to isolate this task from those unrelated merges.

Final integration includes main `d71e4dfd`. Clean `d02159c7` built the ordinary
image without warnings using verified SDK/userland/ports bundles from successful
main workflow [#1707](https://git.internal/PyxisOS/pyxis-os/actions/runs/1707).
These pin userland `fbce73bfaae710ed5dcaa0e5fd3f7fb8416befd0` and ports
`4326582e4bfaad3833b8311f66aedc1f5308f90d`; fs/lwIP and effective config remain
as above. These are inherited main pins, not task 2 gitlink changes.
ELF SHA-256 is `1526850522c780b7dda470dac201e6bdd0d97e82e662bc807cd39eff9f8f5d0d`;
ISO is `53d00d22d1ebbb495b5f4d6f13d5916012426ead3d33411d47d6d6a225df9c5f`.
Subsequent edits only complete this documentation; submitted-head CI is separate.

## Manual qualification

Interactive QEMU/GDB inspection used the matching B ELF with
`set may-call-functions off`; no target calls, target writes, probes, fault
injection or new test/boot infrastructure.

- **1 CPU receipt cleanup:** after a reply consumed its receipt handle,
  `receipt_handle=0`, `state=DELIVERY_COMPLETE`, `receipt_live=true` and storage
  references=1 in `capability_release` from CALL. The local reference was cleared
  before release; `endpoint_receipt_release` then observed references=0. The
  embedded delivery remained owned through admitted cleanup.
- **1 CPU detached CLOSE:** a pipe handle's generation was 2 at admission. Before
  `pipe_grant_release`, its slot was empty, rights/transport zero, generation 3,
  and the table guard unlocked. Final writer release set grants=0 and
  `writer_closed=true` while storage references=1 and both physical ends remained.
- **4 CPU endpoint admission:** RECEIVE on CPU 1 held receiver references=2,
  rights=1 and transport=2 in the endpoint handler (baseline had one storage
  reference). The existing eight-message SEND warmup/pass consumed all eight,
  with zero rejection, status 0 and verified payload.
- **Pipe transfer on 1 and 4 CPUs:**
  `session bin://iobench.pxe pipe --buffer 65536 --rounds 1` verified its warmup
  and sample, each 1,048,576 bytes written/consumed, 256 calls per side, 255 short
  transfers and zero errors. Its existing acceptance requires EOF/content and
  both worker exits. Debugger-paused runs are functional evidence, not timings.
- **Ordinary launch/IPI:** four-CPU remote `echo task2-ipi` and shell exit reported
  status 0 and complete drain. Separate inspection observed BSP rescheduling CPU
  2 from network/readiness service. No panic/assertion or new log line occurred.

Process-stop/fault injection, saturation and concurrent sibling CLOSE were not
run; siblings are not enabled. Error/stop reference pairing and callback lock
order were inspected in source. Task-owned QEMU/debugger jobs were stopped before
timing and after qualification.

The final integrated ordinary image was booted separately on both CPU counts:
1-CPU CALL64 warmup/pass verified eight round trips, and the full pipe warmup/pass
again verified 1 MiB with zero errors. Four-CPU SEND4096 warmup/pass verified eight
messages/32 KiB; GDB observed receiver references=2, rights=1, transport=2 on CPU 2.
Remote `rm boot://share/iobench.bin` was denied and `cat tmp://missing-task2`
reported not found (both status 1); subsequent echo and shell exit were status 0
with complete drain. These are functional checks, not additional cost samples.

## Matched workloads

Capture the baseline before editing. Existing Development commands:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 5
```

Each has a verified warmup plus five timed passes with a fresh receiver; timing
excludes preparation/verification. SEND admission sums bounded batches; completion
includes acknowledgment. Launch uses an untimed 16-echo warmup followed by:

```text
lua -e 'for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'
```

The existing `pyxis-remote --machine --no-shell-echo --columns 80 --rows 24`
client and `/usr/bin/time -f %e` measure the whole launch session through exit and
complete drain, including client/transport/startup overhead. No isolated syscall
latency claim. Clock calibration is retained, never subtracted.

Initial pre-code A medians (min..max), milliseconds except launch seconds:

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| CALL 64 complete | 87.143 (86.584..87.525) | 76.776 (74.870..80.556) |
| SEND 64 admit / complete | 1.182 (1.164..1.961) / 13.537 (12.816..13.594) | 1.172 (1.170..1.208) / 12.886 (12.777..17.907) |
| CALL 4096 complete | 87.433 (86.963..87.931) | 92.185 (91.314..95.615) |
| SEND 4096 admit / complete | 1.247 (1.239..1.251) / 13.656 (12.919..14.857) | 1.268 (1.229..1.509) / 17.863 (12.751..20.100) |
| 1024 launches, seconds | 3.11 (3.10..3.21) | 3.23 (3.22..3.27) |

Five IPC samples per row/CPU and three launch sessions; every IPC sample verified
256 messages with zero failures/rejections, every launch batch counted 1024 and
exited 0 with complete drain. One interrupted preliminary capture was rerun and
excluded; all complete samples remain.

## Interleaved comparison

Measured 11:11–11:31 UTC on 2026-10-10. Run order was three A/B pairs on one CPU,
then three A/B pairs on four CPUs, then the focused one-CPU SEND4096 A/B/A/B
follow-up. Each full profile ran launch warmup/timed session first, then CALL64,
SEND64, CALL4096, SEND4096, with a fresh OS boot between IPC commands. One pair's
CALL64 input lost a keyboard character and launched no benchmark; its invalid
capture was kept separately and the command rerun after SEND64. No complete
sample was filtered. The debugger only read terminal output after completion.

All 52 IPC invocations verified their warmup and five timed passes: **260 timed
passes**, each 256 messages, status 0, verified payload and zero failed CALLs or
rejected SENDs. All 12 timed launch sessions counted exactly 1024, verified the
following `echo task2-launch-complete`, exited both commands 0 and drained
completely; all 12 warmups counted 16. Existing commands/tools only, no saved
benchmark/input runner. Task-owned jobs stopped before the integrated build.

Full-profile median (min..max), 15 IPC samples per cell; launch has three samples,
seconds. Initial baseline and focused follow-up are separate.

| Workload | 1 CPU A | 1 CPU B | 4 CPUs A | 4 CPUs B |
| --- | --- | --- | --- | --- |
| CALL64 complete | 92.735 (88.525..113.924) | 88.713 (79.710..96.274) | 107.498 (98.912..126.857) | 99.654 (76.034..127.819) |
| SEND64 admit | 1.341 (1.089..4.696) | 1.252 (1.183..2.152) | 1.218 (1.180..1.575) | 1.260 (1.068..1.805) |
| SEND64 complete | 14.046 (11.917..44.606) | 14.078 (13.087..17.770) | 15.925 (12.186..22.589) | 17.915 (11.932..25.348) |
| CALL4096 complete | 91.938 (88.386..113.023) | 90.435 (88.501..98.129) | 98.426 (76.118..123.309) | 108.418 (95.845..350.169) |
| SEND4096 admit | 1.303 (1.243..2.091) | 1.334 (1.264..4.649) | 1.387 (1.250..1.839) | 1.402 (1.263..2.928) |
| SEND4096 complete | 14.219 (13.275..14.867) | 14.530 (13.452..54.595) | 18.129 (13.028..23.936) | 17.345 (12.173..33.034) |
| 1024 launches + echo, seconds | 3.50 (3.42..3.51) | 3.43 (3.42..3.52) | 3.90 (3.25..6.50) | 3.42 (3.32..3.87) |

Per-pair completion medians A→B (ms), launch seconds:

| CPUs / pair | CALL64 | SEND64 | CALL4096 | SEND4096 | Launch |
| --- | --- | --- | --- | --- | --- |
| 1 / 1 | 111.820→80.639 | 27.039→14.381 | 108.064→90.435 | 14.172→14.153 | 3.50→3.43 |
| 1 / 2 | 89.846→88.713 | 12.105→13.925 | 88.483→89.208 | 14.219→14.134 | 3.51→3.42 |
| 1 / 3 | 92.203→93.860 | 14.046→14.251 | 91.938→91.536 | 14.358→47.367 | 3.42→3.52 |
| 4 / 1 | 114.515→78.527 | 16.078→17.915 | 112.447→99.567 | 17.605→15.852 | 3.90→3.32 |
| 4 / 2 | 107.017→107.014 | 12.943→18.354 | 109.855→216.135 | 21.768→21.038 | 3.25→3.87 |
| 4 / 3 | 108.681→104.286 | 15.925→16.670 | 83.723→108.418 | 17.598→17.884 | 6.50→3.42 |

The four-CPU aggregate B CALL4096 median is **10.2% higher**, SEND64 completion
**12.5% higher**. These observed increases and the slow valid samples remain.
B's one-CPU SEND4096 pair 3 had completion median 47.367 ms and admission 3.956 ms;
its independent clock calibration was 96,143 ns/read versus A's 41,243. The
focused fresh-boot follow-up did not repeat it:

| Pair | A complete ms (range) | B complete ms (range) | A/B admit median ms | A/B clock calibration ns/read |
| --- | --- | --- | --- | --- |
| 1 | 14.298 (13.156..14.568) | 14.627 (12.986..15.427) | 1.288 / 1.353 | 39,946 / 41,199 |
| 2 | 14.074 (13.428..16.938) | 14.050 (13.224..14.908) | 1.329 / 1.289 | 41,545 / 41,456 |

Four-CPU B CALL4096 pair 2 calibration was 139,629 ns/read versus A's 37,335;
pair 3's extreme did not repeat, but B was still 29.5% slower than paired A.
Unchanged A also had SEND64 calibration 380,600 ns/read and a 6.50-second launch
session. Calibration means were never subtracted. These correlated changes are
not proof of the cause. Broad ranges, differing paired directions and unrelated
host QEMU activity prevent a stable causal-overhead or speedup conclusion;
this record does **not** establish zero overhead. No performance optimization
is implied by these nested-VM results. Raw captures/all samples remain in
the baseline worktree's ignored `build/task2-baseline` and `build/task2-pairs`;
integrated build/debugger captures remain in `build/task2-integrated`.

## Native interleaved A/B

Owner-run ThinkPad T14, 12 CPUs, AC power, fresh PXE boots in order A1/B1/A2/B2.
Luna built A `d71e4dfd` (ELF digest prefix `f5683c13`) and B `ea39ff9f`
(`eaf58495`). All four used the same main `1ef20a1c` initrd (`6e45fbd3`),
including the fixed remote session lifetime from userland #203. Digest prefixes
are owner-supplied, not full hashes independently verified here. The four IPC
commands above ran hands-free through `pyxis-remote --machine`.

Raw captures: `/shared/present/batch2/ab671/{A1,B1,A2,B2}/{call,send}-{64,4096}.jsonl`.
Independent decoding confirmed the reported medians, 80 timed passes (five per
workload/boot) plus verified warmups, status 0, verified payload, zero failed
CALLs/rejected SENDs, and all 16 FINAL records exited 0/drain complete. Native
launch/IPI timings are not part of this run.

Median milliseconds per 256-message pass:

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 1.254 | 1.438 | 1.848 | 1.230 |
| CALL4096 completion | 1.384 | 1.026 | 0.989 | 1.898 |
| SEND64 completion | 0.547 | 0.929 | 0.371 | 1.175 |
| SEND64 admission | 0.092 | 0.129 | 0.060 | 0.131 |
| SEND4096 completion | 1.083 | 1.362 | 1.240 | 1.265 |
| SEND4096 admission | 0.189 | 0.251 | 0.229 | 0.231 |

CALL changes direction across pairs; no stable difference is resolved. SEND64
admission is higher in both B runs: A 0.234–0.359 µs/send versus B
0.504–0.512 µs/send from the rounded medians, an increase of about 0.145/0.277
µs/send in the two pairs. SEND64 completion also rises. SEND4096 is slightly
higher with overlap. Ranges remain wide: SEND64 admission A1 0.055–0.131,
B1 0.057–0.157, A2 0.059–0.068, B2 0.062–0.153 ms; even B includes low passes.
Clock calibration is 130/134 ns/read in A1, 130 in A2 and 136 in both B runs,
reported without subtraction. These sub-millisecond/millisecond native costs
supersede the nested-VM percentages as practical cost evidence; they do not
isolate instruction cost or prove zero overhead.

### SEND cost attribution — source inspection

The zero-attachment **raw SEND** benchmark reaches `call_object` in
`kernel/syscall.c`: old borrowed `capability_resolve` is replaced by
`capability_acquire`, dispatch and `capability_release`. Per SEND, the new path
adds table exclusion (atomic exchange and release store), saturating object
retain (CAS), object release (atomic decrement), and rights/transport snapshot
and cleanup stores. On the uncontended successful path that is three atomic
read-modify-write operations; no new per-message heap allocation. The IF=0 guard
assertion also reads flags and executes CLI. Retain/release implementations
already existed; the additional invocation per syscall is new.

`send_endpoint`/`admit_message` record selection, queue locking, payload copy and
publication are unchanged for this zero-grant path. The exported-client
delivery reference already existed and is not used by raw SEND. No detached
generation bookkeeping runs inside the SEND interval; receipt CLOSE/release and
receiver DRAIN occur outside it. Completion includes those receiver/acknowledgment
paths and scheduling, so it cannot be assigned to SEND dispatch alone.

Each pass times 32 groups of eight using 64 boundary CLOCK calls. Their new
capability admission also contributes: the start read's post-timestamp release
and end read's pre-timestamp acquire fall inside each interval. Therefore the
measured admission increase is consistent with the added guards/references,
but its exact share is unprofiled; dividing by 256 is an amortized batch figure,
not isolated SEND latency.

**Recommendation:** retain this small per-message cost for the accepted owned
admission contract. Removing the guard or retain/release would restore borrowed
lifetime or weaken saturation/authority guarantees. No demonstrated cheap fix
recovers the observed increase. A non-mutating flags read could avoid the
redundant CLI in the guard assertion, but changes the shared lock helper and
does not remove the required atomics; defer unless profiling establishes value.
No optimization or changed benchmark bound is included in this PR.

The current benchmark accepts at most 256 messages and 100 rounds. For optional
stronger sampling without a code change, run the same hands-free loop from
[remote terminal](../../../userland/remote-terminal.md#consumers-and-limits)
with `--messages 256 --rounds 100`, on fresh A1/B1/A2/B2 boots. That adds samples
with fresh receivers; it does not lengthen an individual admission interval.
Longer per-pass `--messages` needs a separately scoped benchmark change. Another
native run is not required for this recommendation.

## Delivery refresh after native results

Merged main `3d4bb733` into the PR after recording the native A/B. The only
conflict was the Neovim status table: retain main's completed libc/port work and
this branch's corrected threads anchor. No task-2 optimization or new task was
added. The measured native B remains `ea39ff9f`; the refreshed integration is
separate qualification, not another native timing sample.

Clean merge head `5e61e525` built the ordinary kernel/image warning-free with
verified main [#1741](https://git.internal/PyxisOS/pyxis-os/actions/runs/1741)
SDK/userland/ports bundles and the same compiler. ELF SHA-256
`c46e24fffc392bb1f54af8c459159c69fb593da9559eb519ae141cc4b384f7af`;
ISO `a74c5a13c232d4e02d73fa078cf0c7bc39f2e6d25ddbb3108c3e26a90aad8809`.
Matched Q35/nested-KVM fresh boots: 4-CPU CALL64 and SEND64 each returned verified
warmup/five passes, summary and FINAL 0/complete; ordinary echo/exit also passed.
1-CPU CALL64 did likewise; pipe warmup/sample verified 1 MiB with zero errors.
The existing reverse client used the documented loopback-beacon NAT adaptation.
Captures are in ignored `build/task2-native-integration`; all task-owned QEMU and
client jobs were stopped. Earlier debugger lifetime inspection remains above.
The PR was already open for review, not draft; exact submitted-head CI is checked
after this documentation update.
