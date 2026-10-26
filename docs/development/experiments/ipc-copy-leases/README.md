# IPC copy-phase lease qualification

Pre-code A: clean main `2fca30fd1d5ee6a7eabc0b285bacf0a6b9b8f5c7`, including
merged #700/#702, 2026-10-11. Exact-head successful
[CI #1822](https://git.internal/PyxisOS/pyxis-os/actions/runs/1822) supplied
verified kernel/SDK/userland/ports bundles. Ordinary image assembly and host
remote-client build passed; LLVM 23.1.3 builder `49e2c1a`, cached owner-mirror
AX200 firmware, no upstream fetch or compiler rebuild. Userland `df210f1a`,
ports `df3210b0`, fs `b427df29`, lwIP `a1aadb91`; no pin changes.

Frozen A ELF SHA-256 `4131ab131dfb33a12fd1f0d9d02a4150d61f46e8b12d1420ea8d0589b1a77525`,
ISO `5800da8fe6b36ab1c95a89380033e6296e0615f9f9b628e4cda9de7bb6dc37ad`,
initrd `ade95a3487ebc89a40f32dff4bef4bfac3887e65ce5eb6b52bb016a9d947f020`.

QEMU 10.2.2, Q35/nested KVM, CPU host, 1/4 cores, one thread per core, 2 GiB,
UTC RTC, matching raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO
SCSI CD/net/RNG. Info logging/profiling off. UDP NAT forward 2324; host reverse
client `--machine --no-shell-echo --listen t14 --beacon-address 127.0.0.1
0.0.0.0 2323`. No debugger/build during timings.

B source: `060abf2686ec181a41059aa95b6bd158a19eaf72`. Ordinary kernel/image
build passed with `make -j16 image PREBUILT='sdk userspace ports'
REMOTE_BEACON=t14` in the same builder. Frozen B ELF SHA-256
`5060e57f89d4cac63a8551ff9a8b5d6bcc55e73b62dea217caa29cb8de1224a3`,
ISO `bfedc1d9ea3b24fe5bc612d2317fa202c10501a9ece09f062de065f65e29f7c0`.
A/B initrd and kernel configuration are identical; later documentation commits
change no qualified source. No ABI, dependency, logging or benchmark changes.

## Interleaved nested-QEMU comparison

For each CPU count: fresh boots A1/B1/A2/B2, CALL64/CALL4096/SEND64/SEND4096 in
that order per boot, 256 messages and 100 rounds plus untimed warmup. All 3,200
timed passes verified with zero failures/rejects and FINAL 0/complete. Clock
calibration 32,630–41,891 ns/read, not subtracted. Values below are median
(minimum–maximum) milliseconds per 256-message pass; no samples removed.

### 1 CPU

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 84.035 (83.403–87.912) | 78.452 (77.183–229.934) | 86.477 (84.334–93.696) | 84.103 (83.626–85.321) |
| CALL4096 completion | 84.362 (83.850–92.869) | 78.939 (77.087–88.892) | 86.666 (84.746–94.316) | 84.522 (83.966–85.654) |
| SEND64 completion | 13.125 (12.892–13.591) | 12.321 (11.652–19.231) | 13.403 (12.828–14.997) | 13.026 (12.822–13.540) |
| SEND64 admission | 1.212 (1.200–1.677) | 1.137 (1.089–1.807) | 1.225 (1.194–1.607) | 1.200 (1.190–1.648) |
| SEND4096 completion | 13.148 (12.904–13.668) | 12.364 (11.788–13.175) | 13.614 (13.112–17.016) | 13.176 (12.590–25.502) |
| SEND4096 admission | 1.254 (1.240–1.490) | 1.205 (1.158–1.608) | 1.311 (1.267–2.202) | 1.269 (1.245–2.404) |

### 4 CPUs

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 81.322 (74.965–112.403) | 98.098 (87.731–123.130) | 101.726 (78.519–126.101) | 81.807 (77.392–99.663) |
| CALL4096 completion | 75.395 (68.222–103.487) | 104.899 (77.306–137.706) | 105.869 (75.135–117.377) | 105.285 (80.237–118.606) |
| SEND64 completion | 13.463 (11.693–20.268) | 16.141 (11.736–33.104) | 13.919 (11.749–27.589) | 16.118 (12.774–23.306) |
| SEND64 admission | 1.246 (1.191–1.916) | 1.250 (1.193–2.859) | 1.280 (1.202–2.773) | 1.223 (1.189–2.004) |
| SEND4096 completion | 12.693 (11.860–31.725) | 17.992 (11.992–27.688) | 18.382 (12.977–25.801) | 15.968 (11.872–25.087) |
| SEND4096 admission | 1.328 (1.268–3.785) | 1.342 (1.259–2.134) | 1.353 (1.273–2.257) | 1.297 (1.249–1.901) |

One-CPU B medians are lower than the preceding A, but overlap across boots.
Four-CPU variation is broad and inconsistent between sequences. Neither
comparison establishes native benefit; native results are recorded below.
Raw captures/decoded values remain in ignored `build/ipc-lease-{baseline,after}`.

## Inspection and runtime checks

Interactive GDB against frozen B on 1 and 4 CPUs, with target function calls
disabled: `read_message` payload prevalidation had process/task borrower counts
1/1, returning to 0/0 before admission. RECEIVE publication copy had 1/1,
returning to 0/0 afterward. Normal CALL completion, SEND admission and CLOSE
returned with 0/0. Existing `server --delivered-timeout` reached cancellation of
a delivered live receipt with caller/provider counts 0/0; the 4-CPU check also
inspected both immediately after `cancel_delivery` and found 0/0. The demo
verified timed-out CALL, rejected late REPLY, receipt finish and retained grants.
`server --close` also completed on both configurations with endpoint-closed
results. Every invocation returned FINAL 0/complete through the remote client.
Small debugger IPC runs used 2 messages/1 round, CALL64/SEND4096 on 1 CPU and
CALL4096/SEND64 on 4 CPUs; these are correctness checks, not timings.

Independent source review confirms inspected no-attachment CALL lease pairs
15 -> 11 (requester 6 -> 5, RECEIVE 5 -> 3, provider REPLY 4 -> 3), SEND admission
4 -> 3. The leased helpers preserve full-range validation and output copy order;
all acquired tokens release on error. Capture-generation checks, CLOSE,
reservation/publication, post-copy REPLY recheck and late-output cleanup are
unchanged. No token spans allocation/growth, scheduling or waits. Syscall/root
admission remains unchanged. Exactly one user task per process remains; sibling
races are source-reviewed, not runtime-qualified here. No injected faults,
target calls, new tests or new klog lines. Task-owned QEMU/GDB jobs stopped.

## Owner native A/B

Owner-run results, 2026-10-11, [posted on #704](https://git.internal/PyxisOS/pyxis-os/pulls/704):
ThinkPad T14 Gen 1, 12 CPUs, AC power, hands-free A1/B1/A2/B2 fresh PXE boots.
A `2fca30fd1d5ee6a7eabc0b285bacf0a6b9b8f5c7`,
B `ce23362d3144611d946232f2a38c6adf441e60c5`; same A initrd and byte-identical
kernel configuration. Owner-built ELF SHA-256 prefixes: A `750cf669`, B
`33cb4a65`. Raw data: `/shared/present/batch2/ab704/`.

The four IPC workloads used 256 messages/100 rounds; values below are fastest
passes, not medians. Pages ×3 and 3 × 1024 launches were also run as in the #700 rerun.

| Workload | A1 / A2 | B1 / B2 | Change |
| --- | --- | --- | --- |
| SEND64, ns/message | 319 / 319 | 307 / 305 | −13 ns (−4%), consistent |
| CALL64, ns/round trip | 3730 / 3873 | 3763 / 3794 | None measurable; A spread 143 ns |
| CALL4096, ns/round trip | 5271 / 4648 | 4555 / 4540 | At most about −0.1 µs; A1 outlier |
| SEND4096, ns/message | 743 / 591 | 617 / 577 | None measurable |

Medians overlap between A/B; pages span 23.7–26.4k ns/op and launches 1.20–1.34 s.
SEND64 recovers 13 of #700's added 97 ns (319 -> 306 ns/message). CALL64/4096
and SEND4096 show no measurable change. The roughly +0.45 µs CALL cost versus
pre-#700 remains after #704; removing four leases did not identify its main
cause. Reply staging allocation/free, the extra payload copy and syscall root
accounting are untested candidates, not attributed causes. The owner parks
profiling until threads task 8 (2026-10-11); task 5 is next for its scope/decision proposal.

Commands on horse after each boot (localhost beacon option above is only for
QEMU NAT):

```sh
mkdir -p "$run_dir"
for mode in call send; do
  for size in 64 4096; do
    printf 'session bin://ipcbench.pxe %s --size %s --messages 256 --rounds 100\n' "$mode" "$size" |
      pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 >"$run_dir/$mode-$size.jsonl" || exit 1
  done
done
```

Set `run_dir` to a distinct directory per boot. Session handoff keeps the shell
parked; no sleeps or trailing `exit` needed. Decode OUTPUT and verify all 100
passes plus FINAL status/drain, as in [task 4](../threads-task4/README.md).
The remaining CALL cost is recorded in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1310).
