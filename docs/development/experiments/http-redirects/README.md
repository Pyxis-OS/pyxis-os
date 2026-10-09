# HTTP redirect qualification (2026-10-09)

Ordinary source images use LLVM 23.1.3/49e2c1a. Baseline Pyxis `ba51452c`,
userland `72f303d6`, ports `8b918c46`; implementation ABI `3cbeeb33`, userland
`5544b63796`, ports `bc04b447eb` (published [userland #181](https://git.internal/PyxisOS/pyxis-userland/pulls/181),
[ports #73](https://git.internal/PyxisOS/pyxis-ports/pulls/73)). Kernel runtime code
is unchanged. Filesystem `b427df29` and lwIP `a1aadb91` stay fixed.

QEMU 10.2.2, Q35 **nested KVM**, `-cpu max`, four CPUs/8 GiB, fresh OVMF,
standard VGA, VirtIO SCSI/RNG/net, host TCP 2397→guest remote 2323. Controlled
Python HTTP servers on 18180/18181 and TLS on 18443/18444 serve fixed bodies and
log requests. TLS uses a disposable CA/SAN `10.0.2.2.sslip.io`, resolving to the
QEMU host gateway. Validation-only initrds append that CA to packaged roots;
a second boot also supplies it through `--ca-bundle` to exercise custom mode.
These assets/keys/configuration and raw logs remain local, never normal images.

| Manual case / command path | Result |
| --- | --- |
| `cksum http://10.0.2.2:18180/code/{301,302,303,307,308}` (five separate commands) | All reach `/final/index.html`: 131 bytes, CRC 1839068259. |
| `/relative/start` → `../final/index.html`, `/scheme` → `//10.0.2.2:18181/final/index.html` | Same exact final bytes. |
| `/cross` → other port → 308 → final | Allowed; two redirects, same bytes. |
| `/loop/a` → `/loop/b` → `/loop/a` | CALL_LIMIT; only a/b contacted, no repeated request. |
| `/hop/2`, `/hop/0` | Ten redirects succeed; excess fails CALL_LIMIT after eleven requests, without contacting the twelfth target. |
| HTTPS `/downgrade` → HTTP | CALL_DENIED; no HTTP target request. |
| HTTP `/upgrade`, public-mode HTTPS `/customcross` → TLS port 18444 | Allowed with fresh TLS verification. This is controlled trust, not a real public-CA interoperability claim. |
| Custom mode: uppercase host `/code/302` | Same-origin HTTPS succeeds with exact bytes. |
| Custom mode: HTTPS `/customcross`, HTTP `/upgrade` | CALL_DENIED; no next-hop target request. Source inspection confirms the gate precedes DNS/TCP/TLS. |
| `links -dump -html-numbered-links 1 .../code/302` and `.../relative/start` | Final page; child reference is `/final/child.html`. |
| Interactive Links on `/relative/start`, then `=` | Info URL is `http://10.0.2.2:18180/final/index.html`; link is its final-directory child. Server records one redirect chain, no adoption refetch. |
| Subsequent HTTP GET and `cksum text://welcome` | Continue succeeding after refused chains; text provider remains compatible. |

The first implementation sitting used userland `7167473`/ports `1373a94`;
final custom-mode sitting uses the revisions above. The intervening changes
initialize diagnostics, explicitly initialize workspace fields and bind enum
values to wire constants; required cases also exercised the corresponding code.
Source review covers remaining-budget enforcement, malformed-grant cleanup,
sticky origin tracking, retained bindings and descriptor metadata lifetime.
No new test framework, fault injection, CI or boot automation was added.

## Matched direct-GET observation

Before implementation, then afterward, three manual
`cksum http://10.0.2.2:18180/baseline.bin` commands returned identical 1,042-byte
bodies/CRC 1742341614. Same server, CPU/device configuration and remote client.
Host time spans FIFO submission to the final command-completion log write;
it includes remote I/O, program launch, fetch and checksum, not pure HTTP latency.

| Image | Three samples, ms | Median / range, ms |
| --- | --- | --- |
| Baseline | 102.004, 44.661, 56.217 | 56.217 / 44.661–102.004 |
| Implementation | 46.445, 43.694, 46.250 | 46.250 / 43.694–46.445 |

Three sequential samples on a shared nested host, including first-use effects,
do not establish a speedup or native cost. No async responsiveness, POST/replay,
real-public-CA redirect chain, native performance, download-dialog or comprehensive
Back/fragment history qualification is claimed. Budget/deadline cleanup beyond
these controlled cases is source-inspected. Owned processes are stopped after validation.
