# Capture handoff and retention

2026-10-15: unchanged kernel main `67c68042`, scope-only documentation head
`f51796c2`; userland `1fce25df`, ports `0e760128`, fs `101e1f68`, lwIP
`a1aadb91`. Ordinary full image builds using the existing
`pyxis-llvm23.1.3-df87791` compiler container and `make -j16 image`, default
`LOG_LEVEL=debug` / info console. No compiler or runtime adaptation.

QEMU 10.2.2, q35/KVM nested inside the shared Fedora VM, CPU host, four CPUs,
2 GiB, UTC RTC, fresh OVMF variables, standard VGA, VirtIO net/RNG. Loopback
24487 forwards to guest 2323. Stock plain Remote is machine/no-echo, 80×24.
Three cohorts each have one warmup plus ten measured samples per workload.
Lua OPEN excludes close; launch includes exit; reads include OPEN/EOF/CLOSE;
write/pipe report their existing transfer/completion intervals. All fixture
checks succeed and all nine sessions exit zero with complete FINAL drain.

| Nested elapsed ms | Cohort medians | All-sample range |
| --- | --- | --- |
| Native OPEN | 0.06011 / 0.10669 / 0.06366 | 0.03961–0.27243 |
| Text OPEN | 2.35718 / 2.17153 / 2.33942 | 0.93465–14.14510 |
| Launch through exit | 9.47649 / 9.09205 / 9.59458 | 2.39117–27.10032 |
| Boot FILE read 1 MiB | 0.31372 / 0.40096 / 0.26950 | 0.26104–0.72358 |
| RAM FILE read 1 MiB | 0.50291 / 0.92336 / 0.47946 | 0.44693–1.23635 |
| RAM FILE write 1 MiB | 10.27215 / 10.05219 / 10.10535 | 9.75951–11.25466 |
| Pipe completion 1 MiB | 1.06196 / 0.93383 / 1.04571 | 0.52754–5.46153 |

Commands for each timed cohort, using the existing Lua and iobench tools:

```text
cp boot://share/iobench.bin tmp://a5.bin
lua -e 'local now=pyxis.monotonic_ns; for _,p in ipairs({"boot://share/iobench.bin","text://welcome"}) do local f=assert(io.open(p,"r")); assert(f:close()); for i=1,10 do local a=now(); local f=assert(io.open(p,"r")); local b=now(); assert(f:close()); print("OPEN",p,i,b-a) end end'
lua -e 'local args={"bin://echo.pxe","-n"}; local now=pyxis.monotonic_ns; assert(pyxis.run(args)==0); for i=1,10 do local a=now(); local s=pyxis.run(args); local b=now(); assert(s==0); print("LAUNCH",i,b-a) end'
iobench read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 10
iobench read tmp://a5.bin --bytes 1048576 --buffer 4088 --rounds 10
exit
```

For cohort N, a separate Remote connection runs
`iobench write tmp://baseline-write-N.bin --buffer 4080 --rounds 10`, then
`exit`. Another connection runs
`session bin://iobench.pxe pipe --buffer 4096 --rounds 10`; successor exit
finishes that session. Each name is fresh. No output/boot automation or new
benchmark program was added.

Afterward, read-only GDB sees all four CPUs idle, no pending retired kernel
objects and zero retired heap bytes. This is an idle observation, not proof
of leak freedom. The owned VM/client/debugger jobs are stopped. Raw command
outputs, JSONL, image, firmware and debugger capture stay in ignored
`build/output-baseline/`.

Limits: another agent's QEMU was present and shared-host load is uncontrolled;
no task-owned build ran during timing. These are nested elapsed observations,
not native hardware costs. Wide ranges constrain attribution. Repeat the same
commands/configuration interleaved with this preserved image after each kernel
prerequisite; the changed-code comparison below does not establish a native performance cost.

Baseline ELF SHA256
`cc685d46ef3f1abbbe7f8fe5d8f7d3730dbdf8717bddd5f749b8b435e87153f2`;
ISO `d1bf91b5d508b522442fba89973294cfcff30267f788eb96754decf3a4c1b314`.

## Handoff qualification

2026-10-15: kernel implementation `7770ba12` plus readiness correction
`a1b70c64`, userland `7795dac6` ([#271](https://git.internal/PyxisOS/pyxis-userland/pulls/271));
other pins and compiler image match the baseline. `make -j16 image` passes in
the ordinary builder. No compiler/container rebuild or new klog lines.

Interactive nested QEMU with the same devices/memory, at one and four CPUs,
runs a private SDK demonstration client through plain Remote. It exits zero
with complete FINAL drain on both configurations. This local client is not a
new installed test or test framework; source and captures stay in ignored
`build/output-qualification/`.

| Check | Observed on 1/4 CPUs |
| --- | --- |
| Owned FILE + held READ clock observation | Source generation becomes BAD_HANDLE; collected value and observation work. |
| Source revocable lineage CUT while queued | Collected describe reports DENIED; read returns DENIED; unrelated held clock still works. |
| Bytes then value, value then bytes, second value | IO; rejected grant remains owned; TAKE returns IO with no published result. |
| Zero write before value | Zero bytes accepted without selecting byte mode; first value succeeds. |
| Final collector CLOSE and explicit CANCEL | Queued pipe writer is released; its reader observes zero-byte EOF. |
| Collector COPY/environment/forwarder target | DENIED; original collector remains valid. |
| CLOSED-only collector poll before TAKE | No COMPLETE event is incorrectly reported. |
| Real `echo` through direct capture | EXITED/0; BYTES; collected 12-byte stdout. |
| Single-source `cat` through direct capture | EXITED/0; VALUE; 67,108,865-byte FILE retains source domain/object identity. |

The large source is sparse RAM, not a dense-data throughput measurement. The
unchanged identity and GDB inspection qualify ownership transport rather than
bulk copy speed. Each child is given its explicit MEMORY grant for libc TLS
bootstrap; the core creates no implicit authority. GDB sees the same FILE
pointer and reference count across atomic source-table extraction, with an
independently held clock grant. After final cleanup both configurations show
idle CPUs, no pending retired objects and zero retired heap bytes. These idle
observations are not a general leak proof.

The first runtime run found readiness entering the pair guard with IF enabled.
The corrected function saves/restores interrupt state like pipe readiness;
all reported passing runs use that correction. Allocation/table-exhaustion,
late reply-copy failure and concurrent cancel-versus-admission races are
source-inspected only. Exported HTTP failure-body transport is not separately
run; CUT lineage/failure and independent observation are exercised above.
A further four-CPU run moves an already-CUT, held failed grant: handoff and
TAKE succeed, describe/read remain DENIED, and the independent observation
still works. Native hardware and shell capability substitution are not claimed.

## Matched ordinary workloads after handoff

Three changed-image cohorts repeat the exact baseline commands (10 measured
samples plus warmup each). A fresh preserved baseline boot is interleaved
between changed cohorts 2 and 3. Every one of these twelve sessions exits zero
with complete drain and verified iobench transfers. No owned build or debugger
runs during timing; other agents' shared-host load remains uncontrolled.

| Nested elapsed ms | Changed cohort medians | Changed range | Interleaved control median |
| --- | --- | --- | --- |
| Native OPEN | 0.13051 / 0.03966 / 0.12984 | 0.03898–0.18242 | 0.03977 |
| Text OPEN | 3.06924 / 1.55722 / 2.08956 | 0.84580–4.96885 | 1.10121 |
| Launch through exit | 8.87065 / 9.10929 / 7.49549 | 2.35507–12.92471 | 7.89938 |
| Boot FILE read 1 MiB | 0.28730 / 0.27922 / 0.28913 | 0.27150–0.77690 | 0.26953 |
| RAM FILE read 1 MiB | 0.50118 / 0.45423 / 0.45690 | 0.43580–0.75567 | 0.45336 |
| RAM FILE write 1 MiB | 9.88253 / 9.97401 / 9.79612 | 9.69950–10.36816 | 10.26264 |
| Pipe completion 1 MiB | 0.71806 / 0.74620 / 0.82630 | 0.53638–4.92811 | 0.72286 |

Native OPEN itself varies by roughly 3× among changed cohorts, with its fastest
cohort matching the control. These shared nested observations overlap the
baseline; they establish functional continuity, not attributable native speedup
or regression. Raw JSONL, commands and summaries stay in `build/output-after/`
and `build/output-control/`; the owned QEMU/GDB/client jobs are stopped.

Changed ordinary ELF SHA256
`db0ba34d8b99e26857228a355159f882dd784f3001ce1702674ab28aa34fac94`;
ISO `2960fd969ad85a9599ebe0bc61fffcc8143fb57099431b08900b3f02688273a3`.
Private demonstration source SHA256
`721a27d5ac929289c2f5c1ff0428354d8e582143345bb52b9cd3c423cd734a96`;
private ISO `04368297e48b7a331d8cc667ecefd7f2392a3d041cd23471f2e72d77132e58c6`.
Additional already-failed case source SHA256
`c98ae7b0c0be8ab96a8003c60841c4d8468315922b6431b2cdd1325cc5ae7d44`;
private ISO `5f331cae408b8e9074d9839a1e77f201a073dac97dbc366bdf91aace8f11d60b`.
Retention remains a separate unimplemented prerequisite: this transport slice
has ordinary unaccounted RAM byte backing and is not A5 completion.

## Admitted producer error correction — 2026-10-15

Daybreak's adversarial run at `81281f2c` / userland `7795dac6` found an admitted
RAM write failure could still collect a prior prefix or empty FILE as success.
The wrapper now records a failed WRITE/RESIZE result under its short guard,
detaches the backing, and releases it after unlocking. Existing active calls
retain their own storage pin; SEAL/TAKE cannot publish the discarded prefix.
The original producer error remains its call result; collector failure is IO.
No retry-after-error contract is introduced.

The ordinary full image build passes. Existing positive SDK demonstration runs
again at one/four CPUs: ordinary byte/value collection, failed-grant and CUT
lineage, mixed/zero/second output, close/cancel and real echo/cat all pass with
EXITED/0 and complete Remote drain. Read-only GDB afterward sees idle CPUs,
no retired objects and zero retired heap bytes. No new reproduction, allocation
injection or test infrastructure was added. The new failure branch is inspected;
Daybreak reruns its independently owned negative case separately. No native or
performance claim. All owned QEMU/GDB/client jobs are stopped.

Correction ELF SHA256
`6baae706833a907104cb8c87d29ed8372de108da13d6fa58d9fcd07f89e44e65`;
ordinary ISO `5e27e4d13dfe31daa24d8753544699e9f8662637a7647d793e08ceb1dcaf9d49`.

A further source review of `6cc2e83e` found byte TAKE's newly issued READ grant
was not derived through a wrapped collector's lineage. Generic validation,
installation and reply copying add no lineage. Byte-only grant derivation now
runs before policy/commit; moved pre-existing primary/observation grants remain
unchanged. Ordinary full build and existing one/four-CPU SDK/GDB checks pass
again, including the already-held failed result; the new wrapped-collector
negative case is source-inspected, not claimed runtime-qualified. No new probe.
All owned jobs are stopped. Final correction ELF SHA256
`3a349c451480f419cb6802aaa45982bf5f82e4b421bc9f99b92427fc1e2b9969`;
ordinary ISO `f6cf02333167fc0ba0ae5c76f4ceeb69459b34579b8199adef66ebeb4524ac1b`.
