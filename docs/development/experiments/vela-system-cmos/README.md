# Vela SystemCMOS operation region

Owner-assigned bug, 2026-10-15. Investigation against main `fecd1439` and the
owner's local firmware tables. The NMI policy below was accepted 2026-10-15.
The native stick now contains installed `2aa4e13e`: any qualification packet
must keep that kernel ABI/archive pairing and copy only the kernel after an
`lsblk` check. No native writes or kernel packet have been performed here.

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

## Bounded proposed implementation

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
The alternative is to defer this handler until a reliable initial NMI-state
handoff and its ownership are defined; preserving an unverified readback is
not an alternative.

## Source and qualification plan

Linux's handler is actually
[drivers/acpi/x86/cmos_rtc.c](https://github.com/torvalds/linux/blob/v6.12/drivers/acpi/x86/cmos_rtc.c#L25-L49).
It serializes byte transfers with the RTC lock and interrupts disabled.
The [x86 CMOS accessor](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/rtc.c#L73-L94)
selects the supplied register directly; it does not read/restore port 0x70.
These are inspected contracts, not copied GPL implementation. The proposed
full-range check deliberately prevents bit 7 of an offset becoming an NMI-mask
write.

Qualification: capture the missing-handler baseline with a local synthetic
SystemCMOS region, then ordinary builds and interactive QEMU/GDB read/width/bounds
checks. Do not write RTC date/configuration in qualification. Build an ABI-matched
Vela kernel-only packet for installed `2aa4e13e`, with hashes and owner
`lsblk`-first ESP copy/restore commands. Native qualification must establish
that real `_Q18` execution no longer aborts; QEMU is not that evidence.

Installer task 5 follows this focused fix; it requires a fresh proposal review
before further implementation.
