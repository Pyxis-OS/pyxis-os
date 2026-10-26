# HTTP shell transaction checkpoint — 2026-10-13

Task 5a is in review; the owner deferred standalone scripts to #789 on 2026-10-13 in
[#780](https://git.internal/PyxisOS/pyxis-os/pulls/780). No native measurements are
claimed. **Pyxis must be able to hit good network targets overall in the future.**
The ThinkPad task 4 1 MiB GET measured about 81–83 MiB/s, versus the historical
85 MiB/s TCP receive reference, not a contract. Relevant changes must track the
gap; these nested results cannot be divided by native TCP to claim a regression.

## Baseline before implementation

Fresh parent `bfd14785`, userland `b38b0e09`, ports `9f407742`, filesystem
`414be0ac`, LLVM23.1.3 builder `4c23a20`. Ordinary image plus SDK/ports/userland/
kernel bundles rebuilt and verified. Nested KVM q35/HPET, max CPU, four CPUs,
2 GiB, VirtIO network/entropy, fresh OVMF; local fixtures only. Direct POST adds
a disposable VirtIO-blk/writeback NPFS disk and the existing manual task 4 probe,
whose private backing is absent from the client's boot/out roots.

| Workload | Median ms (range) |
| --- | --- |
| 1 MiB GET whole, five verified samples | 425.802 (264.685–899.135) |
| 1 MiB RAM whole, five verified samples | 0.731 (0.698–0.874) |
| Ordinary Lua child launch/wait, 20 samples | 8.787 (5.851–37.676) |
| Direct 1 MiB staged NPFS POST, five verified samples | 1502.337 (1496.464–1542.845) |
| POST preparation | 70.694 (50.401–71.409) |
| POST stage/flush | 603.313 (573.997–607.542) |
| POST commit | 852.248 (836.608–864.502) |

Commands: `iobench read http://10.0.2.2:19085/fixed --rounds 5`, RAM control
via `cp boot://share/iobench.bin tmp://http5-ram.bin`, monotonic timing around
`pyxis.run{"echo","-n"}`, and the existing Lua w+ API with 128×8192 z bytes,
explicit flush/commit and complete response/status. Every payload/EOF/hash check
passed; one untimed warmup each. No clock costs subtracted or isolated spawn cost
claimed. Unrelated host compiler load produced broad ranges: these initial
samples do not establish worker overhead/savings. The interleaved comparison
below uses identical producer/backing/data/CPU configuration in one quieter
window; Lua w+ remains direct and must not be labelled a worker path.

## Matched worker costs and GET control

Frozen current image: clean parent `72e13880`, userland `22fcade`, ports
`bcacac6`; later userland `fc22dc5` changes README only. Same builder and VM
settings; private NPFS disk, local fixture, no debugger attached during timed
work. An existing manual probe explicitly declares the shell script's held
PIPE CREATE/LAUNCH grants; these timings precede the public shell bundle and do
not measure its manifest/positional-script startup.

Five alternating direct/worker POST pairs after warmups use the same 1 MiB z
payload (128×8192-byte writes), headers, backing and 120-second deadline. The
server independently matched every length/hash; each child exited zero and
validated its retained 13-byte response. Full time includes script and child
launches, coordinator, response validation and cleanup, not isolated worker CPU.

| Workload | Median ms (range) |
| --- | --- |
| Direct Lua w+ POST | 1520.807 (1479.042–2369.700) |
| Shell worker POST | 1418.610 (1335.966–3157.490) |
| Paired worker minus direct | -60.432 (-169.788–1509.402) |
| Empty ordinary producer, per launch | 6.140 (5.458–7.242) |
| Empty producer through worker, per launch | 10.407 (10.086–10.511) |
| Paired extra empty coordinator cost | 4.371 (2.977–4.949) |

Whole-POST differences cross zero; no reliable overall speedup or slowdown is
established. Five empty pairs each contain 20 identical printf launches, with
PREPARE/ABORT outside the marked interval; the repeatable added 3–5 ms motivates
[revisiting direct WRITE plus reliable producer completion](https://git.internal/PyxisOS/pyxis-os/issues/1486).
Direct phase medians: prepare 52.630 ms, stage/flush 591.538 ms, commit 867.129 ms,
response 4.480 ms, final status/close 0.910 ms. One separately marked worker run
measured about 60/443/845/21 ms for prepare/stage/commit/response+drop; those
intervals include marker launches/waits and are not directly comparable.

GET control brackets the frozen baseline with current/baseline/current boots,
five verified samples per boot, identical devices, fixture and pristine disk:

| 1 MiB complete GET | Median ms (range) | MiB/s at median |
| --- | --- | --- |
| Current first | 185.454 (166.809–196.213) | 5.392 |
| Baseline `bfd14785` | 175.818 (152.110–180.601) | 5.688 |
| Current repeat | 185.943 (149.769–195.951) | 5.378 |

Current medians are about 5.5–5.8% higher with overlapping ranges. This is a
nested control, not a native HTTP/TCP-gap result. Commands use the existing
probe's `run .../direct.sh`, `run .../worker.sh`, empty controls and
`child iobench read http://10.0.2.2:19085/fixed --rounds 5`. Raw sample rows,
fixture scripts, hashes and launch/VM provenance remain local or on the PR.

## QEMU functional evidence

Userland `f317f09` broad RAM suite, `61ed1fa` corrected JSON example/NPFS/TLS;
ports `ff2cb09`, parent task 5 RAM ABI/kernel changes. Source/ISO/SDK provenance
and raw captures remain in local build output, outside the repository.

- Stock declared RAM: exact `printf '{}' > json+http://… => reply`, then
  `cat < @reply`, one 2-byte JSON POST and no new GET. Retained response/STATUS
  copies, shared consumption/EOF, drop and name/env/missing-capture refusals pass.
- Failed producer prevents submission and later manual commit. Truncated readers
  and reader pipelines report actual native child exit 1; a successful final
  checksum does not hide the failing reader.
- Exactly 16 MiB stages/uploads with an independent matching SHA-256;
  16 MiB+1 refuses CALL_LIMIT before mutation, prints the NPFS hint and leaves
  submission not started. Read-only debugger inspection confirms DIRECTORY_RAM
  staging and no backing leaf/ancestor/source in the interactive shell.
- Final runtime `22fcade`: a 5-second original deadline expired with no
  submission or renewal; real foreground Ctrl+C stopped producer/worker and
  refused later commit. A response-only copy retained the exact remaining
  1,048,575 bytes after prefix close/original drop; final drop released backing.
  Explicit optional NPFS unavailable kept GET working and refused POST without
  selecting RAM.
- Stock declared private NPFS with local TLS: 17/32 MiB POST exact server lengths/
  hashes, 32 MiB GET incremental equality/count, truncation and unauthenticated
  TLS EOF failures, readable early 413/500 bodies, and a 35-second response with
  an explicit 60-second deadline pass. The private volume is absent from every
  client root; normal bootstrap creates provider leaves without a test wrapper.

## Installed packaging check

Parent `b6ece128`, userland `17035fd`, ports `bcacac6`: ordinary image and all
four bundles built/verified; complete provider bundle and trusted worker are
rescue roots. Host-shaped filtered archives exercised normal NPFS bin/REVISION
and missing-bin fallback with stock installed startup/default RAM staging.
Normal printf JSON POST and rescue echo equivalent both uploaded exactly `{}`,
retained the 13-byte response with complete framing and caused no GET/replay.
Read-only inspection confirmed private RAM leaves and no client ancestor grant.
The native installer filter was compiled/inspected, not executed: the shaped
archive is a runtime packaging check, not a claimed install/update run.

## Large streaming check

Frozen parent `72e13880`, userland `22fcade`, ports `bcacac6`, the same ordinary
image/configuration, local TLS/private NPFS and existing explicit Lua adapters
with one 7200-second deadline per request:

| Operation | Exact bytes | Final result |
| --- | --- | --- |
| HTTPS GET | 2,148,532,224 (2049 MiB) | HTTP 200, complete framing; native/source/cleanup zero |
| Staged HTTPS POST, 64 KiB repeated-buffer writes | 2,148,532,224 | Same; 13-byte response and successful close |

GET checked every byte/count; POST staged/flushed the full body and the server
independently matched decoded length and SHA-256. Both hashes matched the
expected repeated-z payload. These exercise the existing adapter beyond signed
32-bit size, not a multi-GB shell-worker timing result. At live checkpoints the
provider owned 2,318,336 backed bytes (606,208 allocator backing), unchanged from
32 MiB; Lua GET owned 2,306,048 (462,848), POST 1,990,656 (147,456). These are
sampled capacities, not peak RSS or filesystem/network-cache accounting.

The final worker `22fcade` aligns pipe reads to FILE_WRITE_MAX_BYTES, avoiding
16-byte trailing WRITE calls; its allocated buffer stays 4 KiB. All owned
qualification jobs are stopped. The large transfers use the existing Lua adapter; standalone-script checks are
recorded separately below.

## Withdrawn positional prototype — qualification and split

Clean parent `8cba734d`, userland `cef30297`, ports `1800e285`: ordinary image
and all four bundles built/verified. Same nested VM configuration, local HTTP
fixture, no timings. Stock live, normal native-bin and missing-bin rescue boots
passed bare `shell home://post.pxs`; existing init filenames stayed unchanged.
Installed archives were host-shaped from inspected policy, not produced by an
executed native installer. Five JSON POSTs carried exactly `{}`; three deliberate
GETs checked retained/nested reads and truncation, with no replay or failed POST.

Nested scripts kept independent same-name tables. An unfinished parent POST
correctly caused provider BUSY before child submission; a completed parent GET
allowed independent nested transactions. Positional argv/flag, missing/nonfile
and provider-path refusals, unchanged granted-script diagnostic precedence,
missing LAUNCH and body-producer attenuation all passed. Failed producers and
malformed response readers/pipelines stopped the script before later markers.

**Confirmed blocker:** physical Ctrl+C terminated the positional coordinator,
but read-only before/after snapshots showed the same blocked `cat` and staging
worker still submitted/parked and not stopping. The surviving cat retained
native console-reader ownership. No POST occurred. The parent owns interrupt
arming but terminates only its direct child; the script receives READ alone.
The owner accepted deferral to scoped subgroups (later delivered by [standalone scripts](../../userland/shell.md#script-mode)),
without a console-interrupt handoff. The positional implementation is removed
from 5a and preserved in a blocked draft follow-up. All owned VMs/server/debuggers are stopped;
this failure gates standalone 5b, not the qualified interactive 5a delivery. No native or final bundle-startup timing
claim follows from these functional checks.

## Split-core regression

After withdrawing the positional loader, clean parent `d046e254`, userland
`0d89874` and ports `1b171610` passed stock live and host-shaped normal/missing-bin
installed boots. Each sent exact JSON `{}`, read the retained response and
observed complete HTTP 200/framing success. Only three POSTs reached the fixture;
no GET or cancelled POST. Plain `shell PATH` refused without executing the file.
Both installed forms used real native NPFS bin/system/home volumes; archive
shaping was host-side, not an executed native installer.

Rebased parent `5d2c8b66`, userland `06812e7` (main `fcb0c0e`) and ports
`1b171610` (main `a8b50bc6`) built an ordinary image and all four bundles, with
all verifiers passing. The unchanged nested configuration above passed fresh
stock live startup, exact interactive printf JSON POST/retained read/STATUS and
withdrawn positional-loader refusal. Physical Ctrl+C on a blocked body producer
removed both cat and worker; debugger inspection confirmed no console reader or
surviving stage. These are functional checks, with no new timings. The rebase
retains main's configuration checker and uses the selected directory capability
for staging creation and failed-start cleanup. All owned jobs are stopped.

Standalone [draft #792](https://git.internal/PyxisOS/pyxis-os/pulls/792) and
[userland #250](https://git.internal/PyxisOS/pyxis-userland/pulls/250) preserve the
loader for #789; subgroup integration and cancellation qualification remain
pending there. No console-interrupt handoff is implemented.

## Owner native recheck

Use the same local fixtures and image configuration for main/PR comparisons;
no credentials or paid service. In an ordinary installed space, repeat the
printf JSON session and retained cat/status/drop, producer failure, foreground
Ctrl+C and RAM-cap refusal; confirm fixture counts exclude any new GET or
failed POST. With explicitly selected private NPFS backing, repeat a larger
upload and inspect final uploaded/framing/native/source/cleanup status.

For the network reference gap, run `iobench read FIXTURE/fixed --rounds 5` on
main and the PR, alternating boots in one quiet window, using the fixture's
known 1 MiB iobench pattern; repeat the native RAM control. Record medians,
ranges, NIC/CPU configuration and whole-consumption throughput. Compare the
current HTTP result with the ThinkPad TCP reference above; do not treat 85 MiB/s
as a promised rate or reuse nested figures as native evidence. Worker POST
timing uses the explicit trusted manual profile for the recorded direct/worker
comparison. Public positional script startup belongs to deferred 5b and was not
timed here.
