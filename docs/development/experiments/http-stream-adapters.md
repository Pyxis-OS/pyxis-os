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

**Pyxis must be able to hit good network targets overall in the future.**
The prior [native task 3 A/B](http-prepared.md) measured approximately 58 MiB/s
HTTP versus the current ThinkPad TCP receive reference of 85 MiB/s (send
70.5 MiB/s, 2026-10-09). Those are reference measurements, not contracts. The
new nested figures cannot measure the native task 4 HTTP/TCP gap or its change.
A matched native run remains required before an HTTP-heavy consumer; no native
throughput or resident-memory result is claimed here.

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
  A truncated copy failed before publication.

Lua/Links and final-head integration checks are still being completed in this
PR. Source inspection confirms no body-sized allocation in the stream adapter:
fixed provider scratch/backpressure, bounded native reads, caller-selected FILE
buffering. Lua `read("*a")` and Links' cache deliberately materialize their own
results. This is not a measured process-RSS claim or multi-GB qualification.

## Native follow-up

Use the same LAN fixture and compiler for alternating parent/implementation
boots on the ThinkPad: five 1 MiB iobench reads and RAM controls after warmup,
record OPEN/payload/whole times and verify bytes. Compare whole HTTP throughput
with the contemporaneous native TCP reference and retain the gap. Also exercise
Lua streamed GET/POST and an inherited response reader. Timing and protocol
qualification are separate; no real secret or paid API is needed.
