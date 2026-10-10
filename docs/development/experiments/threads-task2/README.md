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

Interleaved A/B results and final integrated build qualification are pending.
