# Shell conditions, script runtime and merged output

## Before-code baseline (2026-10-18)

Parent `9bcd9743` (accepted plan on code main `31ca5009`), userland `3677edd2`,
ports `33661036`, fs `25337999`, lwIP `a1aadb91`.
Existing builder `pyxis-llvm23.1.3-abc00d0`; ordinary full SDK/image/ports build.
Nested q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, std VGA, VirtIO network/RNG.
HPET remained selected because the guest exposes no invariant TSC. Host load is
uncontrolled; no owned build, debugger or second QEMU ran during measurement.

```sh
make -j8 image REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1 \
  DEBUG_NET= DEBUG_WAIT=0 DISPLAY_FLIP=0 DISPLAY_FLIP_METRICS=0 \
  DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 INIT= NETWORK_CONFIG= \
  MOUNT_DISK= PREBUILT= DOOM_WAD= DOOM_DEMOS= QUAKE_DATA= DIABLO_DATA= DUKE3D_DATA=
```

Three fresh boots A1–A3; each used existing Lua `pyxis.monotonic_ns`/`pyxis.run`
for echo, bundled `shell -c "echo -n"`, and bundled `shell tmp://baseline.pxs`
(the script contains `echo -n`). One warmup and ten measured launch-through-exit
samples per workload per boot. Existing `iobench read` measured verified 1 MiB
native FILE reads with 4,088-byte buffers, one warmup/five samples. A separate
Remote handoff ran `session bin://iobench.pxe pipe --buffer 4096 --rounds 5`:
one warmup/five verified 1 MiB transfers, excluding worker setup from timing.
Every command and final drain passed. These are nested elapsed intervals, not
native latency, isolated CPU cost or a throughput contract.

| Workload | Samples | Median ms [min–max] |
| --- | --- | --- |
| echo launch/exit | 30 | 9.437 [6.273–13.652] |
| shell `-c` launch/exit | 30 | 17.418 [14.380–20.766] |
| positional script launch/exit | 30 | 16.774 [15.501–25.280] |
| native 1 MiB complete | 15 | 0.313 [0.260–0.789] |
| pipe 1 MiB completion | 15 | 1.549 [0.598–4.277] |

Raw commands, image hash/configuration, samples and Remote output remain under
`/shared/build/shell-gaps/` (`before-metadata.json`, `baseline-commands.json`,
`A1`–`A3`, `baseline-summary.json`). The frozen before image is retained for
interleaved comparisons and removed after delivery closes. New syntax has no
before-time. The [shell](../../userland/shell.md) and [script launch](../../userland/script-launch.md) references define the behavior;
this baseline alone qualifies no implementation.

## Conditions and explicit exit (decision 1)

Ordinary SDK/ports/image build passed. Implementation userland `48293504` was
then rebased as `36fb5bbd` onto main `26e7d910` (the independent `net` command);
final integration is rebuilt separately. No kernel, runtime ABI or container change.

Four-CPU QEMU functional checks passed for left association, pipeline precedence,
skipped missing bindings/substitutions/redirects, whole-line malformed-operator
preflight, recovery and fail-fast at list boundaries in scripts and `-c`,
`exit` default/positive/negative/both signed-32-bit endpoints, invalid argument
refusal, ordinary last-stage and captured all-stage pipeline results, failed
byte output, and a readable 1 MiB handed-off FILE despite producer failure.
Ctrl+C stopped `sleep 30 && echo SHOULD_NOT_RUN` without running the next arm;
a fresh command succeeded. Read-only GDB observed `user_exit(37)` and the child
reported 37. Fault, cleanup-failure and retention-latch behavior was source
reviewed, not fault-injected. No native qualification is claimed.

Fresh boots A1/B1/A2/B2/A3/B3 used the same frozen baseline image, workload,
configuration and sample counts. B used conditions source `48293504`.

| Workload | A median ms [min–max] | B median ms [min–max] |
| --- | --- | --- |
| echo launch/exit | 9.145 [6.334–13.098] | 9.759 [6.011–13.672] |
| shell `-c` launch/exit | 16.816 [15.041–21.790] | 16.043 [13.776–21.866] |
| positional script launch/exit | 16.418 [14.515–22.890] | 17.620 [14.102–22.008] |
| native 1 MiB complete | 0.262 [0.241–0.368] | 0.275 [0.269–0.329] |
| pipe 1 MiB completion | 0.738 [0.528–2.825] | 0.926 [0.511–4.670] |

The pipe transfer implementation is unchanged; its median moved within wide
nested elapsed ranges with similar floors. These controls establish no isolated
CPU cost or native regression. Evidence: `conditions-commands.json`,
`conditions-functional/`, `conditions-stop/`, `conditions-A1` through
`conditions-B3` and `conditions-summary.json` under the raw-record directory above.

## Runtime and merged-output qualification

Final userland chain: conditions `36fb5bbd`, runtime `314b5156`, relay `e24b3084`;
ports `c022da5` (the profile code is unchanged from `a9174fb`; only the README
was subsequently clarified). The ordinary SDK/ports/image build and final
four-CPU QEMU conditions regression passed. No kernel ABI, compiler or container
change. The copied shell fixture exposed an existing optional-console error:
console stdin without a named input grant made preparation emit an invalid
handle. The runtime slice now gates that named grant on actual availability.

The existing threadprobe executable, temporarily wrapped in a bundle declaring
memory, clock and threads, passed through two nested scripts and a pipeline
from a rootless bundled `shell -c`. Its real threads/TLS/once/join/free checks
passed. Startup threads survived `unset`; replacing exported `boot` did not
replace the retained interpreter. Missing optional threads reached the named
required-resource error; an absent bundle interpreter image refused script
re-entry while an ordinary native command still ran. A plain native threadprobe
received no threads. Runtime-only extras do not add a retention account; capture
still needs that declared authority. No actual hosted Clang payload was available
in this VM, so compiler execution is not claimed.

Merged-output checks passed for both redirect orders, both pipe combinations,
stdout/stderr byte capture, a verified 1 MiB `cat` byte-copy fallback, successive
script commands, inherited append and two independent FILE sinks. Four users
plus four helpers succeeded; a ninth total process was refused before redirect
files existed. Unsupported, builtin and background forms were refused. Ordinary
pipelines kept last-user status; helper success did not hide a user's status;
captures still checked every user stage. A split stdout/stderr JSON request
produced exactly one accepted POST. Capturing the prior FILE stdout before a
transaction exercised two separate helpers and a second accepted POST.

Real Neovim TUI `:make` used `>%s 2>&1`, ran the nested threads-required bundle,
and collected both stdout and stderr diagnostics as valid quickfix entries,
with `v:shell_error == 0`. Both collection-option values were asserted inside
the editor, which exited cleanly. Ctrl+C during a merged sleeping command
terminated its helper/subtree; a fresh command succeeded. Read-only GDB again
observed native exit status 37. Source review covers masks, lineage, cleanup,
retention failure and helper-error paths; no injected faults, allocation-failure
coverage or leak proof is claimed. Native hardware remains unrun.

Commands and ordinary QEMU output are in `runtime-commands.json`,
`runtime-functional/`, `relay-commands.json`, `relay-functional/`,
`relay-server.log`, `nvim-commands.json`, `nvim-functional/`, `stack-stop/` and
`conditions-final-functional/` under the raw-record directory. Temporary fixture
construction errors were corrected before the final passes; no such run is
reported as a product pass.

## Final matched controls and relay cost

A1/B1/A2/B2/A3/B3 repeated the same workload/configuration after the complete
stack; B is userland `e24b3084` with the merged collection profile. Three fresh
boots per side; 30 launch intervals and 15 verified I/O samples per side.
No owned build/debugger or second QEMU ran concurrently. Other agents' host
work was uncontrolled. All commands and drains passed.

| Workload | A median ms [min–max] | B median ms [min–max] |
| --- | --- | --- |
| echo launch/exit | 12.747 [9.252–33.743] | 10.016 [6.197–32.461] |
| shell `-c` launch/exit | 19.630 [16.308–22.385] | 16.111 [14.137–142.717] |
| positional script launch/exit | 18.223 [14.375–29.099] | 16.481 [13.271–30.208] |
| native 1 MiB complete | 0.326 [0.289–0.375] | 0.311 [0.271–1.018] |
| pipe 1 MiB completion | 0.887 [0.610–2.948] | 0.829 [0.533–2.597] |

The controls, including unchanged echo/native/pipe paths, moved with nested
scheduling/host load. These overlapping ranges and the included 143 ms outlier
do not establish a speedup or an isolated added cost. They show no repeatable
whole-workload slowdown in this run; native results remain unknown.

On each B boot, existing Lua/native-job timing alternated `shell -c` copying
1 MiB with `cat` to a native RAM FILE, with and without `2>&1`. One warmup and
five measured pairs per boot, 15 per variant; resulting file lengths were
checked. Whole launch/copy/relay-drain medians were 39.399 ms plain
[33.266–61.879], 36.564 ms merged [28.815–72.419]. A relay adds a process, pipe
transfer and sole FILE writer; launch and nested scheduling dominate this
end-to-end measurement, so no negative overhead or throughput gain is inferred.
There is no before-time for the formerly unsupported merge syntax.

Evidence: `final-A1` through `final-B3`, `final-summary.json`,
`relay-cost-commands.json` and `relay-cost-summary.json`. The qualified B image
SHA-256 is `2c0598e5248aff1b6107b027c33a117681d075da70300eb5441e4bdf51ec7c2d`.

## Review corrections (2026-10-18)

Userland `e0947cbd` follows `e24b3084`; ports remains `c022da5`. The ordinary
SDK/ports/image build passed. This is correctness qualification; the timing
cohorts above remain measurements of the explicitly named earlier heads.

Captured transactions now refuse an earlier merge onto the reply collector,
or a collector already holding output, before target OPEN. Both configured-CAP
and URI cases preserved the old binding, did not launch the producer, and sent
zero POSTs to the local fixture. The supported request-body merge and the
uncaptured two-helper transaction case still passed. Known unsupported or
non-writable stdout, helper stderr and missing helper-image checks left target
files absent, including an earlier pipeline stage's output. Named service
startup/replacement and paired captured service publication also passed.

Read-only GDB counted `execution_group_create` entries over identical Remote
sessions ending in the same status-37 marker: plain command 2, direct console
merge 2, pipe merge 2; FILE relay 3. The common setup/marker is included; only
the actual FILE relay added a group. No guest function calls were injected.

The existing conditions/captures, runtime, relay and real Neovim TUI `:make`
checks passed again. Ctrl+C on console, pipe and FILE-relay paths reported
termination and allowed a fresh command. The FILE-relay conditional skipped its
next arm; expected relay termination emitted no relay-failure diagnostic. User
commands retain logical stage diagnostics. Unexpected helper fault/termination now
produces EXITED 1 when the users exited; this classification, group-admission
failure and allocation-failure cleanup were source reviewed, not injected.

Evidence under the same raw-record directory: `review-build.log`,
`review-commands.json`, `review-functional/`, `review-server.log`,
`review-groups/`, `review-*-rerun.log` and `review-cancel/`.
Qualified image SHA-256:
`07784a70014d4dcc3cb769c44442c98a2cd3ef23b42006c82d4972bcc71a4dad`.

Final userland `00370cbd` additionally suppresses helper completion diagnostics
whenever cancellation was observed, including a helper exiting with an error
while the subtree stops. The ordinary full image build and focused console,
pipe and FILE-relay cancellation checks passed on this exact head, as did the
relay/POST suite. The broader checks above ran on `e0947cbd`; no new timing
claim is made. Final evidence: `review-final-build.log`,
`review-final-cancel.log`, `review-final-cancel/`, `review-final-relay.log`
and `review-final-relay/`. Final image SHA-256:
`d510678bf982d07c8869127d3157c72d09a00e7940c8da64f994692e983e4337`.
