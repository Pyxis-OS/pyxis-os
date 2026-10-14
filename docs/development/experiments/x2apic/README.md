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

Implementation and qualification are pending. QEMU qualification will cover
x2APIC and xAPIC at one and four or more CPUs, timers/scheduling/IPIs, external
MSI and IO-APIC delivery, and NVMe/HDA/xHCI activity. The owner runs the ThinkPad
regression; vela's native boot awaits Ethernet adapters or a boot stick.
