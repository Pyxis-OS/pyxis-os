# Capability libc baseline and qualification

2026-10-14. Before implementation: parent `a41f7fe4`, userland `8ac71538`,
ports `efe8d877`, fs `d84f8084`, lwIP `a1aadb91`; builder `df87791`.
Ordinary `make -j8 image`. Nested q35/KVM, four CPUs/one thread, 2 GiB,
std VGA, VirtIO net/RNG, stock 80×24 Remote. No task-owned build/debugger during
measurement; host load was not fully controlled (another QEMU was present).
These are nested measurements, not native throughput. Repeat unchanged/changed
boots interleaved after implementation before attributing a difference.

Three warmed cohorts: existing Lua times ten OPEN/close pairs for native and
text targets; `iobench read --bytes 1048576 --buffer 4088` uses ten native/RAM
rounds or three HTTP rounds. RAM is `cp boot://share/iobench.bin tmp://a4.bin`.
HTTP uses the existing local fixture at `http://10.0.2.2:18080/fixed`.
POST uses the existing Lua adapter: prepare `/echo` with `w+`, write sixteen
64 KiB blocks of `z`, explicit commit, read/verify `POST accepted`, close.
Each cohort warms once then measures three POSTs. Clock overhead is not subtracted.

| Workload | Cohort medians (ms) | All-sample range (ms) |
| --- | --- | --- |
| Lua native OPEN | 0.0390 / 0.0394 / 0.0399 | 0.0385–0.0456 |
| Lua text OPEN | 2.066 / 5.169 / 2.258 | 1.525–7.120 |
| Native 1 MiB complete | 0.313 / 0.276 / 0.292 | 0.269–0.410 |
| RAM 1 MiB complete | 0.485 / 0.464 / 0.463 | 0.436–0.566 |
| HTTP 1 MiB complete | 253.448 / 201.138 / 199.743 | 198.903–261.767 |
| POST staging, pooled nine | 188.482 | 175.189–220.129 |
| POST commit/read, pooled nine | 843.440 | 826.485–874.935 |
| POST stage/commit/read, pooled nine | 1031.922 | 1001.675–1092.761 |

Every command succeeded; FINAL reported exit zero and complete drain. All twelve
POSTs had 1,048,576 decoded bytes and matching server SHA-256
`3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
Large variation, especially text IPC, limits inference from this first cohort.
Raw commands/output/server records remain local; no new benchmark infrastructure.
Baseline QEMU/server jobs were stopped before implementation.

**Pyxis must be able to hit good network targets overall in the future.**
The ThinkPad TCP reference remains 70.5 MiB/s send / 85 MiB/s receive (2026-10-09),
not a contract or a nested-QEMU target. Relevant native HTTP changes need matched
comparison to that path. This record has no new native A/B result.

## Functional qualification

Candidate `94427d6d`, userland `62977fa`, ports `0e76012`; same builder and
four-CPU nested configuration. Ordinary SDK/image and both Lua library/interpreter
builds passed. An opt-in manual C program exercised native/text/RAM update streams,
relative opens after chdir, cursor-preserving describe, ENOENT with caller-owned
failure output, EISDIR, plain-C `w+` POST/reply and `w` POST, 17 MiB streamed GET
with complete EOF, and exact 16 MiB POST with an explicit 120 s deadline. All five
commands exited zero and Remote FINAL drained completely. The server counted
16,777,216 bytes with SHA-256
`7113391b76d3603ae3d884e787f67eff94100848f0343d5743b051ff9bfb9078`.
Both Lua versions passed explicit io.open/write/flush/read/close; Lua 5.5 also
repeated flush after commit. The existing interactive shell POST binding,
retained reply, status and drop passed with HTTP 201. No ordinary-image test
program or new benchmark framework is added.

An earlier default-30 s Lua 16 MiB upload timed out with an explicit I/O error;
its automatic cleanup reported a real failure. A subsequent Remote reconnect
failed to launch. Daybreak later confirmed the same provider-fatal publication
path described below; the earlier observation is no longer unexplained. The
large successful C run uses its explicit longer deadline; it does not prove
default-deadline large uploads.

Source review corrected FILE/raw-close exclusion and generation handling, dup2
staging, failed-COMMIT body retention, wrong-protocol rights masking of type errors,
and preservation of UNCERTAIN across abandonment and later issuer ABORTED snapshots.
Lua GC/error-unwind abandonment is accepted and implemented in both versions;
clean shutdown is distinguished explicitly. These are source findings/fixes,
not runtime lifetime evidence. Daybreak's independent qualification below records
the observed lifetime/race/error-body results and their limits. A4 is qualified;
source-inspected cases are not promoted to runtime evidence.

## Matched implementation measurements

The first matched series used common base `d4948f33`, userland `220b807`, ports
`efe8d877`, versus candidate `53674ef7` / `8e180cc` / `0e76012`. Fresh boots
alternated A1/B1/A2/B2/A3/B3. Text OPEN cohort medians were 2.034/1.845/2.118 ms
versus 2.361/4.440/3.459 ms. POST total pooled medians were 1,285.568 versus
2,127.012 ms, with ranges 1,078.254–3,174.519 and 1,079.934–3,826.549 ms;
the third pair was 1,100.829 versus 1,095.830 ms. This variation prevents a
precise causal POST-cost claim. Source inspection found an unnecessary cwd-chain
copy for absolute names and HTTP URLs; the candidate now omits it and retains
owned cwd snapshots for relative opens. Final matched results follow below.

Final candidate `94427d6d` / `62977fa` / `0e76012` versus the same control
`d4948f33` / `220b807` / `efe8d877`. Same builder, q35/KVM, four CPUs/one
thread, 2 GiB, HPET, std VGA, VirtIO net/RNG and echoed 80×24 Remote. Fresh
boots alternated A1/B1/A2/B2/A3/B3 in one window; no task-owned builds or
debuggers ran. Other agents’ QEMU work was present, so host load was not fully
controlled. OPEN includes allocation and retained grant capture; close is outside
its timer. Read timings include final framing/EOF. POST preparation is outside
the timer on both sides: old Lua prepare/commit versus io.open/flush; sixteen
64 KiB writes, reply validation and no close inside the interval. Commands and
sample counts are the same as above. All six cohorts succeeded and drained;
all 24 POSTs matched the 1 MiB byte count and the baseline SHA-256.

| Workload | Control cohort medians (ms) | Changed cohort medians (ms) | Control range (ms) | Changed range (ms) |
| --- | --- | --- | --- | --- |
| Lua native OPEN | 0.043 / 0.048 / 0.049 | 0.048 / 0.140 / 0.080 | 0.039–0.166 | 0.039–0.218 |
| Lua text OPEN | 1.284 / 2.340 / 2.513 | 3.102 / 2.552 / 2.146 | 0.917–7.964 | 0.912–5.753 |
| Native 1 MiB complete | 0.292 / 0.295 / 0.329 | 0.304 / 0.290 / 0.321 | 0.271–0.765 | 0.273–0.724 |
| RAM 1 MiB complete | 0.462 / 0.480 / 0.533 | 0.464 / 0.519 / 0.498 | 0.444–1.023 | 0.442–0.843 |
| HTTP 1 MiB complete | 280.361 / 271.075 / 266.088 | 273.563 / 902.674 / 297.986 | 244.573–287.381 | 273.376–954.627 |
| POST staging | 335.033 / 210.445 / 251.097 | 347.012 / 212.866 / 368.310 | 179.862–355.556 | 212.172–465.074 |
| POST commit/read | 706.816 / 851.307 / 909.750 | 732.446 / 957.515 / 803.967 | 688.926–942.335 | 715.717–1823.500 |
| POST stage/commit/read | 1040.765 / 1078.790 / 1160.848 | 1079.458 / 1169.687 / 1185.264 | 1023.959–1190.652 | 1040.848–2288.573 |

Pooled medians: native/RAM reads 0.302→0.304 / 0.487→0.486 ms; native OPEN
0.046→0.052 ms; text OPEN 2.162→2.295 ms. HTTP GET 271.075→297.986 ms
(about +10%, 3.69→3.36 MiB/s), with a much slower second changed cohort.
POST stage 257.156→362.090 ms, commit/read 851.307→803.967 ms, total
1,078.790→1,134.719 ms (about +5%). Components use their own pooled medians,
which need not sum. The wide ranges and differing cohort behavior preclude a
precise causal overhead claim. Native comparison remains needed; these nested
figures are not evidence that HTTP meets or misses the ThinkPad TCP reference.
Raw JSON, decoded output and server records remain local under build/a4-R-*;
only the commands, revisions and summaries are committed. All task jobs stopped.

## Unread-response cleanup regression — 2026-10-15

Daybreak confirmed that A4's terminal publication treated CALL_ENDPOINT_CLOSED
as a fatal provider error after the last response/control client closed. The
kernel correctly refused publication while the retirement notice remained
queued. This killed shared httpfs and caused the next Remote shell launch to
fail. Daybreak also found this path after the 16 MiB POST timeout.

Userland `f239d47`, ports `0e76012`, parent source `55466b7b` (rebased onto main
`a87aec0c`): publication now skips only CALL_ENDPOINT_CLOSED and continues to
other live facets. The local export remains live until RETIRE acknowledgement;
other publication errors still prevent terminal EOF. No kernel or Remote
launch workaround is included. The full SDK/image build passed using the same
cached `df87791` compiler directly on the host: container startup was stuck in
host-kernel network-device registration. No build rules or compiler image were
changed. Earlier timing tables retain their measured revisions; timings were
not repeated for this cleanup fix or the rebase.

Same four-CPU q35/KVM, one thread, 2 GiB, std VGA, VirtIO net/RNG, 80×24 Remote.
A local server at `http://10.0.2.2:18082/echo` read the request completely and
returned HTTP 200, Content-Length 2, body `OK`. Run the supplied Lua command:

```lua
local f = assert(io.open('http://10.0.2.2:18082/echo', 'w+'))
assert(f:write('abc'))
assert(f:flush())
assert(f:close()) -- leave the response unread
```

Three unread-close runs, two read-before-close controls (`assert(f:read('a') ==
'OK')` before close), and five fresh Remote reconnect/echo sessions all passed.
Each client command exited zero and FINAL reported complete drain; the server
recorded exactly five three-byte bodies with SHA-256
`ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
On the third unread run, QEMU GDB stopped at endpoint description publication:
object 2 had `client_live=false`, status CALL_ENDPOINT_CLOSED. After detaching,
close, the next Remote shell, another complete read and its reconnect passed.
This exercises the reported failure path rather than only an earlier completion.
Raw client/server/debugger records remain local under build/a4-unread-* and
build/a4-reconnect-*. QEMU, server and debugger were stopped after qualification.

Daybreak's earlier QEMU checks observed zero POSTs for `_Exit` and abort, and
exactly one POST after the last of two aliases closed. Those are positive
observations. The broader race/error-body requalification and its limits follow.

## Daybreak requalification — 2026-10-15

Parent `68ba1f03`, userland `f239d47`, ports `0e76012`; four-CPU QEMU.
No new defect was found. This is independent QEMU qualification reported by
Daybreak through the owner, not a native result or another root-run probe.

- Three unread POST closes each delivered their body once; httpfs stayed alive
  and fresh Remote shells launched. Two read controls passed. ENDPOINT_CLOSED
  was accepted only for an already-closed client.
- A live response delivered `AB`, paused three seconds, then delivered `CD`;
  EOF appeared only after the final bytes. A truncated response returned EIO,
  rather than successful EOF.
- Fifty close/write, fifty dup2/write, and one hundred source-close/reopen versus
  dup2 races completed without hangs. Every tagged POST body arrived exactly
  once: 69 dup2 successes and 31 clean refusals in the last group.
- Failed OPEN/COMMIT retained their exact 65,536-byte error bodies after CUT and
  receiver shutdown. A full capability table returned CALL_LIMIT for describe
  with a body while metadata-only describe succeeded; freeing a slot restored
  body capture. After control CUT, describe returned RESOURCE_DENIED while the
  already captured body remained readable.

Limits: kernel allocation failure was source-inspected only, without fault
injection. Provider loss used receiver shutdown, not a separate provider process.
The earlier `_Exit`, abort and final-alias observations remain as recorded above.
A4 is checked in the milestone; later tasks are not started by this qualification.
