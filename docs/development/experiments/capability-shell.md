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

## Matched independent shell slice

Same nested configuration and commands, interleaved **B1, A1, B2, A2, B3, A3**;
ten samples per workload/boot. A is the retained clean `146de8e4` image above.
B uses the same kernel/base plus docs scope `327bcced`, userland `81f674b` and
ports `318c5c3`; no kernel, scheduler or pipe changes. Both images were retained
before the final main rebase. All command completions, verifications, FINAL drains
and three pipe successors per image passed. No task-owned build or HTTP server
ran during timing; another agent's QEMU remained active. Raw data stays local in
`build/a5-matched/`.

| Workload | A cohort medians (ms) | B cohort medians (ms) | A / B full ranges (ms) |
| --- | --- | --- | --- |
| Native OPEN | .03948 / .03952 / .08513 | .03960 / .03992 / .13038 | .03873–.18511 / .03909–.13632 |
| Text OPEN | 1.21523 / 2.08541 / 1.43309 | 2.50733 / 1.89210 / 1.77845 | .83492–4.86727 / 1.07341–4.12792 |
| Launch through exit | 8.80118 / 7.05390 / 7.22730 | 6.72579 / 6.86477 / 9.26618 | 2.41785–9.46030 / 2.49997–10.19209 |
| Native 1 MiB | .27999 / .27309 / .27114 | .26365 / .26360 / .25925 | .26952–.32989 / .25413–.29949 |
| RAM 1 MiB | .45176 / .45618 / .45230 | .44873 / .46633 / .46335 | .43334–.49736 / .43867–.51068 |
| Pipe 1 MiB | .54496 / .62982 / .68788 | .60125 / .75669 / .71917 | .51792–3.63860 / .51640–5.22077 |
| Remote echo | 12.31083 / 14.36712 / 12.64978 | 12.71481 / 12.24851 / 12.88976 | 11.81158–16.41525 / 11.75381–14.69175 |
| Remote pipeline | 20.23590 / 22.45383 / 21.79170 | 21.29157 / 21.06066 / 21.14831 | 18.78702–27.15875 / 18.37775–25.99107 |

Pooled medians put launch at −2.4%, Remote echo at −5.1% and Remote pipeline at
−2.3%, with overlapping ranges; this does not establish a speedup. Text OPEN
(+16.6%) and pipe completion (+17.7%) were slower in the pooled samples. Their
wide ranges and cohort drift, also visible in native OPEN, do not isolate a cause.
The shell now captures exported CAPs and records initial publication provenance;
it adds work around launch, not payload or pipe scheduling. No guest CPU profile
or native regression claim follows from these host-contended nested intervals.
Retest the native workloads with the queued prerequisites; do not use this record
to dismiss a native regression. New syntax has no before-time. The identical
native [after-A/after-C run sheet](capability-model-baseline.md) remains due.

## Functional qualification of the independent slice

Ordinary SDK/image builds in the existing `df87791` compiler; four-CPU QEMU as
above. Runtime checks on the measured B image passed TEXT quoting/empty values,
no splitting/reparse, typed private directory arguments, exported TEXT/provider/
directory propagation, per-stage private authority, unset/replacement, and argv
`name://` spelling. A mixed CAP/TEXT word and missing reference failed honestly;
a failed assignment preserved its old value. The same-name collision refused
before touching an existing output. A line containing unsupported `$(...)` made
no earlier redirect file.

RAM `>`/`>>`, stderr append, Lua child append and Lua 5.1/luv `uv_spawn` FILE
append preserved content. Stock init and Remote boot passed after excluding the
interpreter-only `script` grant from generic inheritance. Existing service
publication/copy worked; explicit CUT made its selected exported descendants
unavailable, so further launch preparation refused until those entries were
removed. `describe` displayed native unknowns and text-provider declarations.
The private HTTP JSON POST `=> reply`, `cat < @reply` and GET/list control passed.
The uv-relay example itself requires console output and rejected FILE stdout;
FILE inheritance was exercised through luv instead.

These are ordinary functional checks, not adversarial/lifetime qualification.
Held FILE redirect selection is additionally source-inspected; this run had no
retained FILE capture constructor or npfs disk. Capture/failed-value/retention and
paired producer qualification remain blocked on delta's prerequisites; A5 stays
unchecked.
