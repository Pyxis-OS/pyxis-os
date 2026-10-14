# SuperSpeed BOT transfer qualification

Owner decision accepted 2026-10-14 in [#824](https://git.internal/PyxisOS/pyxis-os/pulls/824):
try SuperSpeed BOT commands up to 1 MiB before another diagnostic build, retain
64 KiB for other speeds/rejecting devices, then one native A/B campaign. The
[ordinary SanDisk baseline](https://git.internal/PyxisOS/pyxis-os/pulls/824)
and Linux reference remain separate from these nested-QEMU observations.

## Frozen revisions and configuration

A: untouched main `60ec274b04bd3f4301e832f1468284bf384614d1`.
B: compiled `7fcf392ae10ec381f33abcf3ea6a6e78fd3488f7`. This isolates the USB
change against main at task start; the later current-ABI integration is below.
Same userland `6f19f494`, ports
`2296a06c`, fs `d84f8084`, lwIP `a1aadb91`. Existing
`pyxis-llvm23.1.3-e0801c2` compiler, INFO, `make -j8 image` with verified matching
SDK/userland/ports reuse. Ordinary default image build passes; one existing
lwIP shadow warning remains, with no new build warning/error.

| Frozen artifact | SHA-256 |
| --- | --- |
| A kernel | `bad4eb8ed48c9613dfc0c3c2fff6ea66bbcbe6569749f5212c8211cd18734e5f` |
| B kernel | `8c4929119290379ef9a78273c3d9d43db3c10dcb56b4918406e2fb5a18c7df99` |
| Common private QEMU archive | `ee7eb38a7d7d538e2b13b8cdf67ae05012c36e980a0743da92130db5f316434b` |

QEMU 10.2.2, Q35/nested KVM, four host CPUs, 2 GiB RAM, OVMF, VirtIO boot/net/RNG
and qemu-xhci/usb-storage. Each run starts from the same pristine 260 MiB GPT
image with a 256 MiB npfs partition, explicit private RW `usbwrite://` bench root
and RAM home. No native media or ESP access. Underlying drive uses writeback;
USB `write-cache=off` nevertheless accepts real SYNCHRONIZE CACHE, unlike the
SanDisk assumed-write-through profile. Host caches were not forced cold.

## Ordinary samples

The unchanged #824 packet uses 1 MiB `iobench write`/same-volume `copy`, one
warm-up plus three verified samples, `--sync`, growing/prepared outputs and
4080/65536 caller buffers. Transfer/file-sync medians and ranges in milliseconds:

| Workload | A transfer | A file sync | B transfer | B file sync |
| --- | ---: | ---: | ---: | ---: |
| write-4080-grow.bin | 217.815 (213.703..225.507) | 78.684 (65.420..102.290) | 216.334 (216.042..222.749) | 64.281 (62.528..67.034) |
| write-4080-prepared.bin | 145.310 (143.316..149.504) | 67.800 (60.712..68.329) | 147.860 (144.312..154.006) | 52.150 (51.354..53.343) |
| write-65536-grow.bin | 217.265 (216.405..222.987) | 80.324 (79.478..86.037) | 231.841 (227.595..360.951) | 51.617 (45.898..52.684) |
| write-65536-prepared.bin | 145.613 (143.505..146.624) | 68.976 (68.480..78.126) | 144.761 (139.251..148.074) | 34.099 (33.971..35.249) |
| copy-grow | 855.769 (840.330..857.032) | 84.689 (64.634..85.980) | 881.494 (856.609..903.345) | 63.744 (47.583..68.339) |
| copy-prepared | 1135.968 (1134.457..1147.731) | 73.642 (52.139..74.294) | 1176.259 (1162.643..1197.801) | 51.286 (33.704..51.384) |

Three 32 MiB `cat` writes from the RAM-backed boot fixture, each followed by
explicit directory sync, hash, removal and sync:

| Sample | A cat / following sync, s | B cat / following sync, s |
| --- | ---: | ---: |
| 1 | 12.097 / 0.352 | 12.040 / 0.184 |
| 2 | 14.039 / 0.348 | 15.148 / 0.276 |
| 3 | 13.938 / 0.369 | 13.976 / 0.091 |

Combined medians: A 14.307 s (12.448–14.387), B 14.067 s (12.224–15.424).
Ranges overlap. Unrelated QEMU/GDB jobs appeared during B; A had no observed
competing build/debugger. Configuration is matched, but host load was not
controlled across the pair. These are functional/variance records, **not a
throughput acceptance claim**. Copy transfer medians rose about 3–4%, and the
65536 growing-write group has a wide B outlier; native A/B must resolve useful
throughput rather than attributing these host-contaminated differences to code.
Small-read before/after ranges overlap across A/B; their residency is uncontrolled.

Both full packets pass all 357 completions, expected hashes and warm-up/sample
byte verifications. Known payloads are removed and explicitly synced; FINAL exits
0 with complete drainage. Detached fsck passes before/after, and bytes outside
the selected partition are identical. QEMU termination follows cleanup/sync via
the monitor; this is not a guest clean-power or power-loss claim.

## Separate request counts and backing

Existing GDB observers profile the same growing 1 MiB command, warm-up plus
three rounds, creation/reset/readback/sync and observed checkpoint. Breakpoint
stops perturb timing; the following counts cannot predict native time shares.

| Successful physical operations | A | B |
| --- | ---: | ---: |
| All WRITEs / bytes | 251 / 5,140,480 | 195 / 5,140,480 |
| Gathered data WRITEs / bytes | 68 / 4,194,304 | 12 / 4,194,304 |
| Metadata WRITEs / bytes | 183 / 946,176 | 183 / 946,176 |
| Actual SYNCHRONIZE CACHE | 108 | 108 |
| Commits (2-image / 3-image) | 27 (6 / 21) | 27 (6 / 21) |

B executes four 512 KiB, four 464 KiB and four 48 KiB data commands. It removes
56 physical WRITEs (22.3% overall); bytes, metadata writes, journal spans, flushes
and commit-image counts remain unchanged. Final controller is READY, with zero
recoveries, no halted endpoint and both slots free. Endpoint cycle changes show
successful ring wrap. A separate passive GDB observation follows a 464 KiB
OUT TD starting at ring entry 253 across the Link to entries 0–5: eight Normal
TRBs, a chain-preserving cycle toggle, final IOC at entry 5, SUCCESS/zero residue,
USB_OK with exactly 475,136 actual bytes, then IDLE. No debugger calls, changed
arguments or synthetic completion were used for that case.

Matched fresh idle GDB snapshots after directory setup/sync and exited session:
PMM allocated pages 10,928 → 13,767, **+11.09 MiB** for one emulated controller.
Heap live bytes rise 7.50 MiB; controller bulk DMA rises 3.75 MiB. Heap pool
capacity changes separately; these categories are not additive independent peaks.
One kernel-image page also changes total usable capacity. All observed allocator
locks are clear. The prepared four-device budget is per controller, including
unused entries; native backing cost depends on discovered controller count.

## Speed profiles and limits

High speed through a USB2-only QEMU connector (`p2=2,p3=1`, storage port 2) and
full speed through a USB hub (storage port 1.1) both report their real negotiated
speed, retain 64 KiB capture/command bounds and two slots, and pass 1 MiB grow
warm-up plus three verified rounds, sync, SHA-256, cleanup, drainage and detached
fsck. Outside-partition bytes remain identical; zero recoveries/halts/held slots.
An attempted `p3=0` controller was refused by existing capability validation
before BOT; it is excluded from these successful profiles and not a new bug fix.

1 MiB raw ticket capture/admission was inspected, but injected GDB `block_submit`
hit the documented NX return-trampoline fault before the controller worker ran.
The disposable guest was discarded without replay; independent whole-disk hash
confirmed no write. Thus **executed 1 MiB READ/WRITE is not qualified** here;
ordinary executed data reaches the existing 512 KiB npfs batch ceiling. Stock
QEMU offers no identified genuine oversized-command rejection setting:
`scsi-hd,max_io_size` reports a VPD limit without enforcing it. No new harness,
QEMU patch or synthetic device-response injection was added. Zero-processed
fallback and short/stall retirement are source-reviewed; their negative large-TD
paths remain runtime-unqualified. This is separate from successful wrap/profile
coverage and from any native result.

## Current-ABI integration after rebase

The capability-forwarder work merged during qualification. The USB branch was
rebased linearly onto `4db9e431`; USB/store source is unchanged apart from a
comment. Fresh ordinary default image builds reuse verified current SDK/userland
`5cd789a`, the same ports/fs/lwIP pins, and the existing e0801c2 compiler. Current
A is untouched `4db9e43141f35f2f66b6febb8143abfab88f6e6f`; B is compiled
`1eb0144231bb6fa5bfb9072ae7e4010bec4b2e02`.

| Current frozen artifact | SHA-256 |
| --- | --- |
| A kernel | `7ad643be449ddb37e5556525d8b9c689b7d52580b3d1ee95bc20aecbb972e4f3` |
| B kernel | `50e2243e9e9da2e7019e20f023421f322f9aebb7d9d2cfba07aac7ea69f8f0ba` |
| Full source archive | `26f62a76381db273edc22067cc5f3556ea59c6a21f6eace1bfd5727931eaa06c` |
| Common private QEMU archive | `a409feaaac59f35c9c9762c251bd017980190567a5425fa471651166eda77504` |

Both fresh current-ABI boots pass 12 functional completions: growing 1 MiB write
warm-up plus three byte-verified rounds, one 32 MiB cat, explicit sync and known
SHA-256s, cleanup/listing and FINAL/drain. Detached fsck passes; outside-partition
bytes match. Background reclamation seen at the first debugger stop drains to
READY/zero recoveries/free slots before termination. These are functional checks,
not new timing samples or an update to the earlier isolated performance record.

## Native packet

`/shared/present/usb-large-bot/native-packet/README.md` contains the current frozen
A/B kernels,
one full source archive, the reused #824 helpers, exact private RO discovery and
RW binding/staging/restoration steps, checksums and one A1/B1 campaign. It uses
horse's existing `pyxis-remote` beacon/listen path, one session at a time. Host
checks pass: Python compilation, dry-run command/96 MiB budget bounds, Lua syntax,
trusted archive re-extraction and identical-archive A/B staging in a local fixture.
Actual GUIDs stay owner-local; staging never edits the served menu automatically.

Native measurement is **pending replacement disposable media or a re-flash**:
the measured SanDisk now holds Fedora. A different stick gets its own A baseline.
No native connection/reboot or real ESP write occurred during this task. A later
diagnostic build is only for whatever gap remains after the owner-run comparison.
Raw captures and excluded setup attempts stay private; all owned jobs are stopped
at the submitted checkpoint.
