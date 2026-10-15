# Vela SystemCMOS operation region

Owner-assigned bug, 2026-10-15. Investigation against main `fecd1439` and the
owner's local firmware tables. The NMI policy below was accepted 2026-10-15.
Current packets use a matched kernel and full live archive; native media is
deployed only by the owner/coordinator.

## Failure and exact firmware operation

The owner reported an unavailable SystemCMOS handler at
`\_SB.PC00.AWAC.RTCM`, aborting EC `_Q18` at its first CMOS field read.
The supplied DSDT declares `RTCM` as SystemCMOS base 0, length 0x3f, with
ByteAcc fields. `_Q18` performs these raw-byte transfers, in this order:

| CMOS register | AML source | EC shared-memory destination |
| --- | --- | --- |
| 0x04 | AWAC.HOR | Q_EC.HOUR, offset 0x54 |
| 0x02 | AWAC.MIN | Q_EC.MINT, offset 0x55 |
| 0x00 | AWAC.SEC | Q_EC.SECN, offset 0x56 |

After all three stores, it sets bit 0 of Q_EC.TIMF at offset 0x57. Those EC
fields live in ERAM SystemMemory, not the SystemCMOS region. The method does
not decode BCD, wait for RTC update completion or change CMOS. The generic
handler must preserve those requested raw-byte semantics. Other AWAC fields
include date/year/century and register A; `_GRT`/`_SRT` implement their own
higher-level time handling.

With today's abort, the time bytes and completion flag are not delivered.
There is no evidence that this firmware handshake is optional, so suppressing
the error or returning invented time is not a correct fix. No general EC query,
power-button or time-setting redesign is proposed.

The checksum-valid FADT has IA-PC boot flags 0x3: CMOS RTC is not marked absent.
Raw firmware stays local; no raw tables, serials or unrelated inventory belong
in this PR.

## Bounded implementation

Install a root SystemCMOS handler before namespace initialization so AML can
attach its regions. Keep uACPI lifetime and AML ownership on the existing BSP
worker. Keep hardware access in the x86 arch layer, with complete validation
before any I/O: widths 1/2/4/8 bytes, entire absolute offset range inside
[0, 128), overflow-safe bounds and little-endian byte assembly. uACPI already
supplies the region-adjusted absolute offset; do not add the base twice.
Unsupported accesses return an error without partial I/O. Refuse the legacy
path when FADT says the CMOS RTC is absent. No second CMOS bank, RTC IRQ,
periodic clock service or alarm feature is added.

One short IF-disabled BSP transaction covers the index/data sequence. No NMI
handler may access CMOS. Current main has **no kernel RTC port reads** to race:
UTC is seeded from Limine's date-at-boot response. Any future kernel RTC access
must share this arch owner; neither an allocator lock nor an AP mutation change
is needed. Trusted AML already has firmware-described SystemIO authority;
SystemCMOS transfers obey that firmware-access model, not a new program grant.
The Vela `_Q18` qualification exercises CMOS reads and its existing EC writes.

## Owner decision — accepted 2026-10-15

**Accepted: explicitly leave legacy NMIs unmasked during and after
SystemCMOS accesses.** Write only the validated 7-bit register index to port
0x70, then access port 0x71. Do not pretend to preserve an unknown firmware NMI
mask by reading port 0x70. This establishes an explicit kernel policy rather
than depending on whatever mask the loader happened to leave. It does not
change local-APIC NMI routing or introduce CMOS reads from NMI handlers.

Rationale: bit 7 of the index write also controls the legacy NMI
gate. Pyxis has no existing shadow/policy for it, and readable/restorable
port-0x70 state is not a portable contract. A default that enables a gate
possibly left masked changes hardware behavior and must not be silently chosen.
No firmware-mask readback or handoff is claimed.

## Sources and qualification

Linux's handler is actually
[drivers/acpi/x86/cmos_rtc.c](https://github.com/torvalds/linux/blob/v6.12/drivers/acpi/x86/cmos_rtc.c#L25-L49).
It serializes byte transfers with the RTC lock and interrupts disabled.
The [x86 CMOS accessor](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/rtc.c#L73-L94)
selects the supplied register directly; it does not read/restore port 0x70.
These are inspected contracts, not copied GPL implementation. The
full-range check deliberately prevents bit 7 of an offset becoming an NMI-mask
write.

QEMU 10.2.2 / Q35 / nested KVM, one CPU, 2 GiB, OVMF and a local synthetic
71-byte SSDT reproduced the missing handler: its byte read returned
`UACPI_STATUS_NO_HANDLER`. With the code-only fix, that AML read succeeded.
Direct read checks accepted widths 1/2/4/8 and byte 127; invalid offset 128,
cross-boundary 127+2, widths 0/3, null output and UINT64_MAX were refused,
leaving the output sentinel unchanged. No CMOS data writes were performed.
Both guests continued through startup of their four configured spaces; owned
QEMU/GDB jobs are stopped.

This functional comparison used the existing matched `75a8aaac` kernel/archive
pair, then `c2a3b13e` (that base plus code-only `18416c66`) with the same archive.
The baseline compiler was e0801c2 and the patched compiler df87791. It is neither
a timing comparison nor exact-current-main runtime qualification. The synthetic
AML and debugger evidence remain local at
`/tmp/pyxis-cmos-baseline/build/HANDOFF.md`; no native firmware was loaded into
QEMU. Current-main qualification also includes the ordinary info kernel build
and exact-head CI recorded on the PR.

The main info kernel builds with LLVM builder `pyxis-llvm23.1.3-df87791`.
The first native trace packet on installed `2aa4e13e` was unreadable because
per-idle trace output flooded the console; its installed rescue archive also
lacked `log`. That packet is superseded, not native qualification.
The owner also reported that its normal installed entry did not mount the
stick, while rescue did. This is **unexplained** and was seen only with the old
spam-heavy trace build. Console-induced boot delay is an owner hypothesis,
not an established cause. The new full-live-archive packet cannot qualify that
installed-pool regression; no mount fix or resolution is claimed here.

Owner refresh, 2026-10-15: rebase #868 onto main `674bf98e`, including #851's
removal of per-idle/per-exit trace lines, #865's archive rescue diagnostics and
#867 / userland #268 log levels. Compile trace records, retain the default info
console threshold and read trace from the ring rather than flooding the screen.
Build a trace kernel and assemble its matching **full live initrd** together;
do not pair the new kernel with the stick's old filtered archive or depend on
pool executables. Reuse only the verified unchanged SDK/userland/ports bundles
from the identical-input #867 build (Actions API run 2653, UI run 2564):
userland `1fce25df`,
ports `0e760128`, fs `101e1f68`, with matching ABI and SDK content checks.
No compiler rebuild, raw firmware or private game data is needed.

Refreshed packet `/shared/present/vela-system-cmos/` carries `caelum.elf`,
`initrd.cpio`, `SHA256SUMS`, the exact source revision and build provenance.
Deploy with the owner's `stick-deploy ~/shared/present/vela-system-cmos --initrd`;
it verifies sums and preserves/restores the installed baseline. The packet's
README contains only the functional check steps, not manual ESP copy commands.
Use the rescue entry and archive-qualified diagnostics (`boot://log.pxe`) so
the check does not require a matching installed pool program revision.

The trace-only EC query completion status includes success, enabling native
proof: `ACPI: embedded controller _Q18: no error`. This confirms the final TIMF
store, rather than merely seeing no error. Normal/info builds add no completion
line. Owner native qualification remains pending; if `_Q18` does not run, the
case is unexercised. No CMOS date/configuration writes in qualification.

Installer task 5 follows this focused fix; it requires a fresh proposal review
before further implementation.

## Native observation — 2026-10-15

The owner ran two untouched 120-second windows on Vela **on battery**, not AC,
with identical bounded read-only instrumentation and matching live archives:
A `dcbcb47b` (main `67c68042`, without this handler), B `3cc39038` (#868
`08088148` plus the same observer). Captures stay local at
`/shared/present/vela-ucsi-ab/ucsi-{a,b}.txt`; raw firmware is not committed.

| In-window result | A | B |
| --- | ---: | ---: |
| SCI / unclaimed SCI | 1521 / 3 | 1460 / 8 |
| EC queries / `_Q16` / `_Q18` | 780 / 779 / 0 | 781 / 779 / 0 |
| UBTC Notify80 / duplicate cached CCI / changed CCI | 779 / 778 / 0 | 779 / 778 / 0 |
| Query failures | 0 | 0 |
| Longest query | 1,269 us | 1,000,156 us (`_Q0B`) |

Both retain VERSION `0x0100` and CCI `0x20000002`. The repeated UCSI behavior
is present at the same rate with and without #868. SCI chronology is not identical.
Post-window trace lines are excluded from the table. No SystemCMOS/aborting
error appears, but **neither run executed `_Q18`**: this is not native handler
qualification. QEMU proves handler dispatch/range behavior; source proves the
missing handler aborts the real firmware handshake. Recommendation, pending the
owner: merge that bounded repair on QEMU plus source evidence, explicitly keep
native `_Q18` coverage open, and handle the independent UCSI client separately.

Firmware `_Q0B` writes its marker, sleeps 500 ms, queues battery Notify(0x81),
sleeps another 500 ms and queues battery Notify(0x80). It never acquires ECMT.
Its recorded start is 5,066 ms and its duration matches those two sleeps; this
is **not evidence of ECMT contention**. `_Q16` uses ECMT, refreshes UBTC's
message/version/CCI mirror, writes its EC transport handshake and queues Notify.
The diagnostic only reads that cached mirror; no UCSI commands were issued.

At every captured unclaimed scan, PM1 status is zero, PM1 enable is 0x20, and
all 28 GPE register pairs have no enabled pending status. GPE 0x6e retains the
software runtime mask but its hardware enable is zero while EC work is queued;
EC SCI_EVT is clear. This explains why uACPI reports unhandled at the scan.
Previously latched/stale SCI delivery is plausible, **not established**. No
SCI/GPE/backoff fix or general EC contention claim follows from these captures.


## CCI interpretation and proposed follow-up — pending owner

The [UCSI specification, revision 1.2, sections 3.1–3.2 and 4.3–4.5.4](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/usb-type-c-ucsi-spec.pdf)
decodes VERSION `0x0100` as 1.0.0. CCI `0x20000002` has connector-change
indicator 1 and bit 29, ACK-command completion. Data length, busy, error and
ordinary command completion are zero. ACK completion may coexist with a
connector indication and clears on the next command. It must not itself be
acknowledged. The cached bit does not identify who issued the prior ACK: the
observer issued no commands and has no transaction correlation.

A pending connector change without a consuming client is consistent with the
unchanged repeat notifications, including on battery. It is not yet proof that
an ACK will stop this firmware's repetition. `_Q16`'s `USGC=0xe1` is the EC
transport handshake, not UCSI ACK_CC_CI. The unclaimed-SCI observations above
also do not justify changing EC draining, GPE enablement or SCI backoff.

Two owner decisions are proposed, not accepted:

1. **CMOS release gate. Default: merge #868 on the bounded QEMU checks and
   source evidence, retaining native `_Q18` execution as an open qualification
   item.** Neither absence of errors nor the UCSI comparison is a native CMOS
   pass. The existing missing-handler abort is known; waiting for an unrelated,
   untriggered query need not delay its repair.
2. **Minimal UCSI client. Default: a serialized ACPI UCSI 1.0 client in
   `kernel/acpi/ucsi.c`, initially limited to the validated ACPI transport and
   observed wire layout.** Bind the actual identified device and validated
   resources; use its standard `_DSM` write/refresh functions, with typed AML
   results rather than integer-only evaluation. Initialize once with a bounded
   PPM interface reset (not a connector reset), enable command-completion
   notification, discover capabilities, then enable only the supported
   connector notifications this client consumes, retaining command-completion
   notification in that replacement mask. Read each reported connector's
   initial status. For a pending connector: GET_CONNECTOR_STATUS, validate and
   retain the reply, then ACK_CC_CI for the successful ordinary command and
   consumed connector indication. Command failure must not acknowledge the
   connector change. Wait for ACK completion without acknowledging that ACK.

The proposed client has one outstanding command, coalesced Notify input and
fixed state storage. Notify callbacks only mark pending work. Its state machine
advances from the existing ACPI worker's top-level loop; it never waits for
completion inside Notify, where deferred EC/Notify work could not run. Use an
absolute 5-second command/ACK deadline, a 10-second reset deadline and sleeping
20 ms refresh polls only while pending, with bounded work per turn. Duplicates
do not renew deadlines. A failed/uncertain transaction parks the client with a
bounded report; no automatic reset, ACK or retry loop. Firmware AML itself can
still sleep, as `_Q0B` demonstrates; these are client-state bounds, not a new
AML execution timeout guarantee.

This authorizes no PD role/power requests, alternate-mode policy, connector
reset, USB data-path change, userspace interface or SCI/GPE policy change.
Native qualification must correlate submitted commands, status replies and ACK
completion, then compare notification/query rates and held-button operation
under the same battery and AC setups. Reduced repetition remains unproven.

The inspected [Linux v6.19.10 core](https://raw.githubusercontent.com/gregkh/linux/v6.19.10/drivers/usb/typec/ucsi/ucsi.c)
separates command/ACK completion and acknowledges connector changes only after
successful consumption. Its ordinary completion wait is 5 seconds and reset
budget is 10 seconds. Its initialization resets the PPM; the specification makes
that initial reset optional. The proposed single initial reset is an explicit
part of decision 2, not an implied permission to reset connectors. The
[ACPI transport](https://raw.githubusercontent.com/gregkh/linux/v6.19.10/drivers/usb/typec/ucsi/ucsi_acpi.c)
uses `_DSM` revision 1 functions 1 (write) and 2 (refresh), matching Vela's AML.
These are design references, not vendored GPL implementation.
