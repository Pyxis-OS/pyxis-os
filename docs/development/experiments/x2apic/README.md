# x2APIC bring-up

Scope accepted by the owner, 2026-10-14: always request x2APIC from Limine when
the CPU supports it. Keep xAPIC MMIO for CPUs without x2APIC. Select and verify
one local-APIC mode at boot; use its register access for EOI, priority, timer and
IPIs. x2APIC sends one 64-bit ICR MSR write with a 32-bit physical destination,
without delivery-status polling. CPU records, topology and debugger lookup keep
full IDs; the existing scratch window still bounds dense CPU indices to 256.

IO-APIC and MSI retain their existing 8-bit physical destinations. An external
interrupt target above 255 fails boot with a clear diagnostic before programming
that route; no interrupt remapping, destination truncation or alternate routing.
No new MADT processor parsing: Limine supplies the CPUs, while MADT supplies the
existing IO-APIC/override routes.

The vela IdeaPad's firmware enables and locks x2APIC. The previous MP request
cannot boot it; Caelum also explicitly rejects x2APIC. Its owner-provided MADT
inventory has 16 enabled type-9 entries, IDs 0..70, no type-0 entries, one
IO-APIC, two overrides and one type-10 NMI. This is firmware evidence, not a
successful native Caelum boot.

## Implemented path and QEMU qualification

Implementation `8c92440f`, baseline main `fcf3def5`, LLVM 23.1.3 builder
`pyxis-llvm23.1.3-e0801c2`, unchanged pinned SDK/userspace/ports. The ordinary
kernel/image build passed. Mode checks precede APIC access; unsupported external
destinations fail before route writes. The [SMP reference](../../../kernel/smp.md#local-apic-and-external-destinations)
describes the MSRs, ICR fences, debugger publication and capacity bound.

Manual QEMU 10.2.2 boots used nested KVM, Q35, 1 GiB RAM and fresh OVMF variables.
A local launcher wrapper selected `-cpu host` or `-cpu host,-x2apic`, retaining
the standard VGA/PS2 setup and adding a modern `qemu-xhci` with `usb-mouse`,
`intel-hda` with `hda-duplex` and a none audio backend, and a disposable 32 MiB
NVMe namespace (`mdts=6,max_ioqpairs=4`, 512-byte blocks). VirtIO-SCSI boot CD
avoided the stock host AHCI CD-ROM crash. No host device or native writes.

| Configuration | Observed result |
| --- | --- |
| Baseline, host CPU, four CPUs | Existing xAPIC path reached the live spaces; AP timers advanced, NVMe reads and USB enumeration completed, HDA became ready. |
| x2APIC, one CPU | Mode confirmed in GDB, timer advanced, shell echo and USB motion worked; hardware breakpoint caught NVMe interrupt entry from vector 46. |
| x2APIC, four CPUs | All CPUs online, AP timers advanced; reschedule vector 37 observed; thread runtime probe passed. NVMe reads, USB motion and HDA stream interrupts worked. |
| xAPIC (`host,-x2apic`), one CPU | MMIO mode confirmed, timer advanced, shell echo and USB motion worked; NVMe interrupt entry from vector 46 observed. |
| xAPIC, four CPUs | Same scheduler/thread/USB/audio checks passed; reschedule vector 37 observed. |
| x2APIC, sparse APIC IDs 0 and 256 | Both CPUs online; AP 256's timer advanced; GDB caught vector 37 on that CPU, and the thread runtime probe passed. |

The four-CPU interactive commands were `pcm 440 440 2` and
`session boot://threadprobe.pxe runtime`. Each mode acquired/released one audio
stream; HDA interrupt counts were 214/213 respectively. These prove delivery,
not audible output or latency: QEMU used a none audio backend. Every NVMe scan
completed three reads with writes disabled. USB reports advanced xHCI event/IRQ
counts; PS/2 tab navigation and typing exercise IO-APIC delivery.

The sparse-ID boot used `-smp cpus=1,maxcpus=258,sockets=2,cores=129,threads=1`
plus `-device host-x86_64-cpu,socket-id=1,core-id=0,thread-id=0`; only two CPUs
were present. This tests full destinations without enlarging the dense-CPU
scratch window. Raw logs, GDB transcripts and captures stay local. QEMU/GDB
jobs were stopped after qualification.

TLB-flush/NMI opcode paths, debugger rendezvous and the above-255 external-target
refusals are source-reviewed; they were not independently triggered. No new
test framework or instrumentation was added. QEMU does not establish native
firmware-lock behavior or universal device-routing compatibility without
interrupt remapping.

## Owner native check

On the ThinkPad, boot this ordinary image with the usual command line; there is
no new boot option. The existing timer preparation summary should say `x2APIC`
on a supporting CPU even when firmware initially selected xAPIC. Check desktop
keyboard/touchpad/TrackPoint, switching spaces, a shell command, mux split, a
short audio/game launch and clean restart. Report any missing device interrupts,
freeze or AP startup failure. Native qualification remains owner-run; vela's
boot awaits Ethernet adapters or a boot stick.
