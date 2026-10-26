# HTTP stream adapters qualification

2026-10-13, native HTTP task 4. Ordinary C/Lua URL reads now use prepared
STREAM responses; cp materializes explicitly. TLS stays in the provider. This
record distinguishes nested-QEMU checks, source inspection and native work.

## Revisions and configuration

Initial matched A/B/A/B used these actual source heads (parent gitlinks in B
were awaiting publication):

| Image | Parent | Userland | Ports |
| --- | --- | --- | --- |
| A: unchanged task 3 | `2f2109fc` | `9f781e94` | `f93becb9` |
| B: task 4 | `7b050e39` | `75d5523` | `d732dd4` |

Both were built with published `pyxis-llvm23.1.3-4c23a20`, ordinary `make -j8
image`, after the owner's LLVM rebuild finished. fs `414be0ac` and lwIP
`a1aadb91` were unchanged. Later source review retained per-scheme bindings
through the chain and removed obsolete FILE snapshot scratch; final qualification
records those revisions below. No compiler-container change belongs to this task.

QEMU 10.2.2, nested KVM, q35, `-cpu max -smp 4 -m 2G -rtc base=utc`, HPET,
fresh OVMF variables per boot, standard VGA, VirtIO network/usernet and entropy,
no HDA. Timed ordinary images had no disk or custom helper. Functional checks
used a dedicated GPT/npfs disk, explicitly private provider staging and a manual
C exercise under ignored build output; no ordinary image contains that program.
HTTP fixtures listened on host loopback 18080, disposable-CA HTTPS on 18443.

## Matched timing

Four alternating boots; five byte-verified 1 MiB samples plus warmup for each
GET/RAM workload. Commands:

```text
iobench read http://10.0.2.2:18080/fixed --rounds 5
cp boot://share/iobench.bin tmp://http4-body
iobench read tmp://http4-body --rounds 5
```

All 40 samples and eight warmups completed with verified bytes and EOF. Existing
QMP keyboard input and read-only GDB console inspection ran outside the timed
workloads; no concurrent build or live debugger. Another agent's FAT32 guest
remained active, so shared host load was not controlled.

| Boot | GET OPEN median (range), ms | Whole GET median (range), ms | RAM whole median (range), ms |
| --- | --- | --- | --- |
| A1 | 384.175 (367.647–405.599) | 616.657 (611.281–661.549) | 0.475 (0.460–0.501) |
| B1 | 16.338 (11.208–32.636) | 344.252 (314.918–350.834) | 0.494 (0.487–0.516) |
| A2 | 569.684 (565.829–573.439) | 1124.177 (1114.290–1143.580) | 0.523 (0.473–0.558) |
| B2 | 29.023 (27.849–30.357) | 625.247 (592.246–689.835) | 0.502 (0.486–0.978) |

A OPEN fetches the entire snapshot; B OPEN returns after headers and payload
work continues in reads. Whole consumption is the fair comparison. Each B whole
median was about 0.56 times its preceding A median, but B2's range overlaps A1.
Large variation across boots prevents a uniform speedup claim. B1/B2 whole
rates were 2.905/1.599 MiB/s; clock controls varied 36.553–135.571 microseconds
per GET clock read and were not subtracted. Raw samples/captures and exact
commands stay in local `build/http4-lab/`, rather than the repository.

A final pair covered the rebased source and retained-binding/FILE-refusal fixes:
A stayed at the table's frozen heads; B's built parent was `d6825b87`, userland
`ab7e00bc`, ports `d732dd4`. Current parent `2316d097` differed only in docs.
Same compiler/configuration/commands, five samples plus warmup per workload.
All 20 samples and four warmups verified.

| Final boot | OPEN median (range), ms | Whole GET median (range), ms | RAM whole median (range), ms |
| --- | --- | --- | --- |
| A | 268.800 (200.652–386.339) | 546.007 (454.970–643.354) | 0.465 (0.442–0.513) |
| B | 33.365 (19.854–44.809) | 1001.803 (779.455–1371.745) | 0.540 (0.505–0.630) |

This final nested whole-GET median regressed **1.835×** (1.832 → 0.998 MiB/s).
One-minute host load rose 1.06 → 16.15 during A, then 17.39 at B's end; another
one-CPU hosted-VM probe was active. That observation does not establish which
work caused the regression. Earlier B results and final B differ substantially;
no final throughput improvement or native attribution is claimed. OPEN still
returned earlier. Raw final samples, memory inspection and captures remain in
local build output. No optimization follows from these measurements in task 4.

### Provider backing capacity

After those timed workloads, read-only GDB inspected existing VM records while
providers were parked, address spaces OPEN and no pending VM mutator. It summed
owned RANGE_BACKED pages, without target calls or instrumentation. Frozen A's
memory was measured retrospectively, not before implementation. The benchmark
itself retains a separate 1 MiB verification buffer on both images.

| HTTP provider | A | B |
| --- | --- | --- |
| Exercised provider owned backing | 721 pages / 2,953,216 B | 451 pages / 1,847,296 B |
| Exercised private-service backing | 1,257,472 B | 135,168 B |
| Idle provider private-service backing | 69,632 B | 69,632 B |
| Exercised minus idle owned backing | 290 pages / 1,187,840 B | 16 pages / 65,536 B |

These are observed retained allocation capacities: libc keeps freed heap pools.
They exclude page tables, kernel TCP/IPC/filesystem buffers, external mappings
and the client; they are neither live HTTP bytes, complete RSS nor true peaks.
Source inspection separately explains removal of the body-sized snapshot below.

## Native A/B — owner, 2026-10-13

ThinkPad T14 Gen 1 AMD on AC, Ethernet LAN, PXE; A main `0e95af3a`, B parent
`24faa688` with userland `ab7e00bc` and ports `d732dd4`, both built in `4c23a20`,
each booting its own kernel/initrd. The owner ran A1/B1/A2/B2, five verified
1 MiB samples per boot against the same horse fixture:

```text
iobench read http://192.168.0.213:18080/fixed --rounds 5
```

| Boot | OPEN median (range), ms | Payload median (range), ms | Whole GET median, ms | Whole MiB/s |
| --- | --- | --- | --- | --- |
| A1 | 12.803 (12.26–13.89) | 5.136 (4.98–5.27) | 17.976 | 55.6 |
| B1 | 1.606 (1.53–1.70) | 10.423 (10.20–11.57) | 12.071 | 82.8 |
| A2 | 11.980 (11.90–12.15) | 4.599 (4.57–4.64) | 16.615 | 60.2 |
| B2 | 1.859 (1.61–1.92) | 10.382 (10.29–17.52) | 12.363 | 80.9 |

Whole GET was about **28% faster natively**; the nested regression did not
reproduce on this hardware/workload. OPEN now returns after headers, so payload
reads include network time. Task 4 recovers task 3's whole-GET cost for this
1 MiB fixture; this does not prove the per-READ cooperative overhead disappeared.
The owner's [PR comment](https://git.internal/PyxisOS/pyxis-os/pulls/774) records
the result; raw captures remain on horse under `~/xf/774{A1,A2,B1,B2}`.
This A/B predates the pushback correction below; no post-fix native timing or
native memory result is claimed.

**Pyxis must be able to hit good network targets overall in the future.**
About 82 MiB/s HTTP is close to the current ThinkPad native TCP receive reference
of 85 MiB/s (send 70.5 MiB/s, 2026-10-09). Those references are not contracts or
a simultaneous TCP comparison. Track the HTTP/TCP gap on relevant changes and
HTTP-heavy workloads, including larger bodies and POST; one 1 MiB GET does not
qualify those workloads. Keep the [throughput revisit](https://git.internal/PyxisOS/pyxis-os/issues/1451).

## Functional checks

Manual C qualification observed:

- Fixed/chunked 1 MiB and fixed 17 MiB GETs, exact byte pattern, complete status
  and successful EOF; 64 KiB descriptor reads still used bounded native chunks.
- Staged w/w+ POSTs, explicit single commit, separate/same response FILE as
  appropriate, 404 body retained as a complete response; precommit read/pushback
  refused with EBUSY and no submission. Server length/hash matched staged bytes.
- Truncated fixed response returned its prefix then ferror without EOF. Raw TLS
  EOF on a close-delimited response likewise failed; authenticated HTTPS GET/POST
  completed. Response seek/stat refused.
- Child response stdin had only STREAM READ and STATUS-only observation, no
  roots/namespace/control; it read verified bytes, obtained final URL/status and
  could not ABORT. Missing, malformed and aliased companions refused launch.
- Relative redirect, loop, ten-hop exhaustion, HTTPS downgrade and HTTP upgrade
  into custom trust followed the existing refusal policy. No target contact
  occurred for policy refusals.
- cp materialized 17 MiB; after normal pool flush, read-only structural checking
  passed and extracted bytes matched SHA-256
  `47c5726efb41b54248182988e40ae5abd9ca50724eeb8f3d214e790917410adc`.
  A truncated copy failed before publication. Final-head recheck over an existing
  destination left its 661 bytes/hash unchanged and no `.cp-` file behind; the
  flushed pool again passed structural checking.

Final functional image: parent `d6825b87`, userland `ab7e00bc` (rebased onto
`c472de7`), ports `d732dd4`; ordinary and manual fixture builds passed in the
same published builder. Lua consumed 17 MiB in 64 KiB chunks, rejected seek,
reported truncated input as an error, committed w/w+ with separate/same FILE
ownership, and retained complete 404 bodies. Precommit EBUSY followed by staging
and commit succeeded. A 17 MiB HTTPS POST also completed; the server independently
confirmed length and SHA-256 `553373126b11a81d6cea970e44a4f9fc01270a3d32f6b578240144acdb882a46`.
HTTPS GET delivered the same 17 MiB byte pattern. Close/abort before commit generated no POST, confirmed by
server records. Links `-dump` consumed a redirected page and printed its final
content with exit zero. A C explicit 120-second HTTPS deadline completed a
35-second delayed body beyond the ordinary 30-second budget; the same endpoint
through ordinary fopen timed out with ferror, no EOF and failed final framing.
Early reader close
released the exchange; another GET then completed.

Source inspection confirms no body-sized allocation in the stream adapter:
an 8 KiB provider queue and approximately 131,000 bytes of active fixed dynamic
provider scratch at the audited maxima, excluding TLS, allocator pools,
image/stack, retained metadata and kernel buffers. The baseline 1 MiB GET also
allocates its 1 MiB snapshot body. Both benchmark clients retain their own 1 MiB
verification buffer. Bounded native reads and caller-selected FILE buffering
replace body-sized transport allocation. Lua `read("*a")` and Links' cache deliberately materialize their own
results. The retained-capacity measurement above is not a process-RSS/peak claim.
Multi-GB qualification belongs to final task 5, not these 17 MiB checks.

### Pushback review correction — 2026-10-13

Userland `da098fd` rejects `ungetc` and the equivalent `ungetwc` path for
PROTOCOL_STREAM bindings with ESPIPE. Ordinary build and a manual equivalent of
daybreak's probe passed in the same four-CPU QEMU setup: before data and after
validated EOF, pushback failed with ferror, left feof unchanged and returned no
injected byte. After clearerr, the byte pattern continued at its existing cursor;
after terminal EOF the next read still returned EOF. Regular native-file ungetc
still returned its pushed byte, and staged w+ retained precommit EBUSY. The
probe remains local build output; no test program enters ordinary images.

## Future native comparisons

Use the same LAN fixture and compiler for alternating parent/implementation
boots on the ThinkPad: five 1 MiB iobench reads and RAM controls after warmup,
record OPEN/payload/whole times and verify bytes. Compare whole HTTP throughput
with the contemporaneous native TCP reference and retain the gap. Also exercise
Lua streamed GET/POST and an inherited response reader. Timing and protocol
qualification are separate; no real secret or paid API is needed.
