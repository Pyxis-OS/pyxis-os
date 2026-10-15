# Capability shell baseline

2026-10-15, **before implementation**: parent main `146de8e4`, userland
`f239d479`, ports `0e760128`, fs `101e1f68`, lwIP `a1aadb91`; builder
`pyxis-llvm23.1.3-df87791`. Ordinary source image build with `make -j8 image`
in that existing compiler container. No compiler or source adaptation.

Nested QEMU q35/KVM, `-cpu host`, four CPUs/one thread, 2 GiB, std VGA,
VirtIO net/RNG, stock Remote 80×24 with quiet input. Host loopback port 24486
forwards to guest 2323. No task-owned build ran during measurement. Another
agent's QEMU was active (about 1.3 host CPUs); host load was not controlled.
These are nested timings, not ThinkPad results or isolated guest CPU costs.

Three cohorts, ten measured samples each, with warmup. Existing Lua times
OPEN through FILE creation (close outside the interval) and small-program
launch through exit. Existing `iobench` verifies each 1 MiB fixture/pipe result.
Remote command intervals use the existing baseline runner's `Runner.remote`
with a local-NAT CLI adapter, from host command send through command completion;
they include host/transport/terminal delivery. No new guest benchmark program.

| Workload | Cohort medians (ms) | All-sample range (ms) |
| --- | --- | --- |
| Lua native OPEN | 0.03997 / 0.03979 / 0.03953 | 0.03870–0.26845 |
| Lua text OPEN | 2.60166 / 1.93974 / 2.44495 | 1.14290–5.16467 |
| Small launch through exit | 7.78157 / 9.89035 / 8.34263 | 2.83364–11.85968 |
| Native 1 MiB complete | 0.282 / 0.273 / 0.288 | 0.270–0.322 |
| RAM 1 MiB complete | 0.459 / 0.457 / 0.449 | 0.438–0.521 |
| Pipe 1 MiB completion | 0.69960 / 1.16841 / 0.78043 | 0.52799–4.92978 |
| Remote `echo -n` | 14.45392 / 13.40619 / 15.82957 | 12.24995–19.00015 |
| Remote `echo -n \| cat` | 22.32532 / 23.12951 / 21.55729 | 20.10903–27.61044 |

Commands, each cohort:

```text
cp boot://share/iobench.bin tmp://a5.bin
lua -e 'local now=pyxis.monotonic_ns; for _,p in ipairs({"boot://share/iobench.bin","text://welcome"}) do local f=assert(io.open(p,"r")); assert(f:close()); for i=1,10 do local a=now(); local f=assert(io.open(p,"r")); local b=now(); assert(f:close()); print("OPEN",p,i,b-a) end end'
lua -e 'local args={"bin://echo.pxe","-n"}; local now=pyxis.monotonic_ns; assert(pyxis.run(args)==0); for i=1,10 do local a=now(); local s=pyxis.run(args); local b=now(); assert(s==0); print("LAUNCH",i,b-a) end'
iobench read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 10
iobench read tmp://a5.bin --bytes 1048576 --buffer 4088 --rounds 10
session bin://iobench.pxe pipe --buffer 4096 --rounds 10
```

Each cohort also sends eleven `echo -n` and eleven `echo -n | cat` commands
sequentially in one fresh Remote connection, discarding each form's first sample.
The pipe uses a separate connection: `session` retains declared creation/service
authority and ends only that session, not the daemon. All command completions
succeeded; all connections reported complete FINAL drain, and all three pipe
successors exited zero. Raw JSON/output/windows remain local under
`build/a5-baseline/` in the shell worktree. QEMU was stopped after the run.

No changed-code comparison yet. Repeat matched unchanged/changed cohorts
interleaved after implementation; the wide launch/text/pipe ranges limit causal
claims. New syntax has no before-time. The separate identical native
[after-A/after-C run sheet](capability-model-baseline.md) remains due.
