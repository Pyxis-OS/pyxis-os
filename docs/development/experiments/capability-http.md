# Transparent HTTP: before-code baseline

2026-10-15, B6, before any implementation. Parent `41c3c902` (main `ffb58ef6`
plus scope/acceptance documentation only), userland `a6040708`, ports `318c5c39`,
fs `101e1f68`, lwIP `a1aadb91`. Existing `pyxis-llvm23.1.3-df87791` builder,
ordinary `make -j8 image`, with host Kconfig modules selected through PYTHONPATH.
No compiler rebuild. Preserved ISO SHA-256:
`e2732ca4d62e8180d6b493ee490e030fd44279ca95aec103abb65f26dbd7ad55`.

Nested QEMU q35/KVM, host CPU, 4 CPUs, 2 GiB, std VGA, modern-only VirtIO
network/RNG, OVMF, quiet stock Remote 80×24. Local NAT 24486→2323;
existing HTTP fixtures on host loopback 19088, guest `10.0.2.2:19088`.
Three warmed cohorts on one boot, not three independent boots. Other QEMU
workloads were present and host scheduling/load was uncontrolled. These are
nested elapsed intervals, not native results or isolated guest CPU attribution.
No owned build/debugger ran during timing; clock overhead was not subtracted.

Each cohort uses existing Lua for ten native/text OPENs; existing `iobench` for
five verified 1 MiB reads each from boot, RAM and HTTP; and one POST warmup plus
three samples. POST writes sixteen 64 KiB `z` blocks through plain Lua
`io.open(uri,"w+")`, explicitly flushes, reads and verifies `POST accepted`, then
closes. Stage includes OPEN/write, commit/read includes flush/response/close.
All command completions and Remote FINAL drains succeeded. All 12 POST bodies
(including warmups) arrived exactly once in this run, with 1,048,576 bytes and
SHA-256 `3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
This is a byte-count observation, not an adversarial exactly-once proof.

| Elapsed ms | Samples | Pooled median | Range |
| --- | --- | --- | --- |
| Lua OPEN boot://share/iobench.bin | 30 | 0.03961 | 0.03757–0.05135 |
| Lua OPEN text://welcome | 30 | 2.57617 | 1.32949–4.99816 |
| boot://share/iobench.bin complete | 15 | 0.27284 | 0.25603–0.30265 |
| tmp://b6-body complete | 15 | 0.46952 | 0.45055–0.83865 |
| http://10.0.2.2:19088/fixed OPEN | 15 | 18.73758 | 13.75767–21.58573 |
| http://10.0.2.2:19088/fixed complete | 15 | 245.53188 | 204.42612–292.61822 |
| POST stage | 9 | 374.47278 | 280.56572–443.96342 |
| POST commit/read | 9 | 893.34380 | 824.69066–958.83774 |
| POST whole | 9 | 1247.84007 | 1184.60312–1332.52831 |

Commands and raw JSON/output, server records, configuration, provenance and the
preserved image remain local under `pyxis-capability-shell/build/b6-before/`.
Use the identical inputs for matched interleaved unchanged/changed runs after
implementation. Report OPEN separately from complete consumption: the proposed
snapshot changes which interval contains download/spooling work. The current
whole-GET median is about 4.07 MiB/s; it does not predict native performance.

Provider resident-allocation observation and backing-charge/release inspection
have **not** run. Before code, complete that observation against this unchanged
image after the pending root-publication decision. Source-inspected constants
(8 KiB response queue, 16 MiB RAM POST cap, old 64 MiB heap collector budget) are
not measured resident memory. No changed-image or native B6 results exist.

**Pyxis must be able to hit good network targets overall in the future.**
The native TCP reference is 70.5 MiB/s send / 85 MiB/s receive (ThinkPad,
2026-10-09), not a contract or nested target. Track the HTTP/TCP gap and retained
snapshot storage cost rather than treating faster OPEN as faster consumption.
All owned QEMU/fixture jobs were stopped after collection.
