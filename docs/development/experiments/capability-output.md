# Capture handoff and retention: before-code baseline

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
prerequisite; no changed-code comparison or implementation is claimed yet.

Baseline ELF SHA256
`cc685d46ef3f1abbbe7f8fe5d8f7d3730dbdf8717bddd5f749b8b435e87153f2`;
ISO `d1bf91b5d508b522442fba89973294cfcff30267f788eb96754decf3a4c1b314`.
