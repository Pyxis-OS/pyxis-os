# Capability HTTP retirement: before-code baseline

2026-10-16, nested QEMU only. Before source implementation: parent `be010d92`
(accepted B8 documentation on code main `bea92e98`), userland `cff500e6`,
ports `f75c3a1`. Three separate 4-vCPU/2-GiB q35/KVM boots, host CPU model,
VGA/VirtIO network/RNG; builder `pyxis-llvm23.1.3-df87791`. No owned build,
debugger or second QEMU ran during measurements; host/CI load was uncontrolled.
Same B7 build flags: REMOTE_BEACON=t14, LOG_LEVEL=info, LOG_UDP=1,
DEBUG_NET empty, DEBUG_WAIT=0, flip/timing disabled, no game assets or init override.

Existing Remote runner and local B6 GET/POST fixture, no new guest benchmark.
Each boot used iobench READ with one warmup/five samples at 1 MiB and 4088-byte
buffer on boot FILE, copied RAM FILE and HTTP; Lua ordinary `io.open("w+")`,
16 × 65536-byte writes, explicit flush, response read/close (one warmup/three
samples); `pyxis.run` echo launch/exit and cp HTTP-to-RAM (warmups excluded).
B7 typed capture, reread and interim `=>`/`@` were also exercised; their Remote
windows include host/protocol overhead and are not pure syscall latency.

| Workload | Samples | Median (min–max), ms |
| --- | --- | --- |
| native open | 15 | 0.041 (0.040–0.044) |
| native payload | 15 | 0.213 (0.197–0.242) |
| native complete | 15 | 0.290 (0.274–0.320) |
| ram open | 15 | 0.039 (0.039–0.097) |
| ram payload | 15 | 0.385 (0.357–0.459) |
| ram complete | 15 | 0.460 (0.432–0.534) |
| http open | 15 | 179.622 (170.601–242.136) |
| http payload | 15 | 0.589 (0.574–0.812) |
| http complete | 15 | 180.259 (171.230–242.777) |
| POST stage | 9 | 225.038 (206.093–242.750) |
| POST commit/read | 9 | 871.483 (728.404–898.679) |
| POST whole | 9 | 1100.521 (934.497–1141.428) |
| echo launch/exit | 15 | 8.366 (6.183–10.635) |
| cp HTTP to RAM, launch/exit | 9 | 201.915 (194.966–264.658) |

Each cohort checked 10 GETs and 6 POSTs, correct POST lengths/hashes, and both
retained reads. One earlier partial cohort used unavailable Lua `os.execute`;
it is excluded, and completed cohorts use the existing native `pyxis.run` API.
No API was added for the measurement. No native result or HTTP/TCP attribution
is implied. **Pyxis must be able to hit good network targets overall in the
future**; the existing [native B6/TCP references](capability-http.md) remain the
comparison, not a contract. Matched before/after and navigation-frame cost remain
for implementation; there are no after figures yet.

Raw commands, Remote output, fixture checks, configuration, revisions and image
hash are local under `/shared/build/http-b8-delivery/` (`results/A1`–`A3`,
`before-metadata.json`). The retained before image is for matched repeats and
is removed when the task closes. Provider-negotiation decision pending in the
[milestone](../../wip/capability-model-implementation.md#task-8-scope-retire-the-interim-surface-2026-10-16).
