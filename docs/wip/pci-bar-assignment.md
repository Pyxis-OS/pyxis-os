# PCI BAR assignment

Status: **owner-requested scope, 2026-10-15; three defaults proposed, no
implementation go.** Kernel resource capability, not a vela-specific address
workaround. [Vela's touchpad](vela-i2c-touchpad.md) is the first consumer and
native qualification target; its task 2 remains blocked here.

## Evidence and boundary

Vela at `7003c117` reaches checked D0: PMCSR `0xb -> 0x8`, BME off, 10 ms
wait, unchanged BAR values. LPSS `8086:e378` BAR0 is memory64/base zero, so
bootstrap mapping refuses. No HID descriptor read succeeded. The saved native
record is linked from the touchpad task PR #894.

Current [PCI discovery](../devices/pci.md) logs BAR bases without retaining their
sizes, ROM allocations or bridge forwarding windows. Mapping permits some
reserved-memory overlaps; it is not a physical allocator or vacancy check.
ACPI runs after AP startup, while current claim/mapping preparation is earlier.

Vela's I2C0 companion supplies no MMIO `_CRS` or fixed address. `IC00` is an
ECAM offset; `POVR` is a power-policy result helper. No documented I2C
PCI/ACPI-mode shortcut was established. PC00 `_CRS` builds its windows from
live `M32B/M32L`, `M64B/M64L` and conditional firmware overrides; raw AML
constants and Linux's observed assigned address cannot substitute for them.

## Proposed first slice

One BSP-owned kernel PCI resource manager reserves physical **bus-address
extents**, distinct from PMM RAM and kernel virtual mappings. Existing native
drivers request an unassigned memory BAR through their exclusive claim; no
userspace BAR-programming API or new capability-object type. The service is
reusable across drivers. Vela's already-fenced LPSS is only its first caller.

Do not move an assigned BAR, resize an aperture, rebalance devices, allocate
I/O-port BARs or expansion ROMs, change chipset personality, or add PCI hotplug.
ROMs and existing downstream windows still participate in exclusions. Preserve
all existing assignments and driver ownership. Nonzero bus-to-physical
translation is refused initially, with bus/physical types kept distinct so
future architecture adapters can support it without changing the allocator.

## Owner decisions — proposed defaults

1. **Claim-scoped memory assignment.** Support unassigned standard 32/64-bit
   memory BARs of claimed endpoints under a supported ACPI root. For a child
   behind configured PCI bridges, intersect the root aperture with every existing
   ancestor window; never enlarge one. Refuse unsupported/ambiguous routes.
   Require a driver-proven quiescence fence, IO/MEM/BME off and masked interrupts
   before probing only the selected BAR (both halves for 64-bit), restoring
   probe values on every exit. LPSS's verified D3hot handoff can provide that
   fence before its first MMIO reset; waking must keep decode/BME off through
   sizing and assignment. Use actual mask-derived size/alignment, not a 4 KiB
   assumption. Match aperture attributes; prefer eligible 64-bit space for a
   64-bit BAR, with below-4-GiB fallback; 32-bit placement stays below 4 GiB.
   Verify both written halves and decoding before publication. One attempt per
   BAR; retained assignments are boot-lifetime in this first slice. Uncertain
   writes retain the claim/range and keep decode/BME off, not a guessed rollback.
2. **Conservative collision proof without foreign sizing writes.** Capture live
   root `_CRS` apertures plus ACPI consumer/motherboard reservations, and account
   for every decoded BAR/ROM and bridge window, boot memory map, explicit
   firmware reservations, framebuffer and CPU-specific MMIO. Windows contain
   allocations; they are not themselves free allocations. Ancestor windows
   constrain a request, while other branches' windows exclude it. Known extents
   use existing owned sizing or validated read-only standard size metadata
   (for example current Resizable BAR control); no changing the configured size.
   Unknown foreign BAR sizes require a conservative proven bound:
   a standard power-of-two BAR's nonzero base alignment and address width bound
   its possible size; reserve that whole possible extent, not a page around its
   base. Zero/ambiguous active decodes, overflow, malformed inventory or an
   unbounded reservation make the affected aperture unavailable. Never write
   probe masks into an unowned function. Generic reserved memory is not free;
   a root `_CRS` window alone is insufficient. If exclusions leave no proven
   size-aligned gap, refuse honestly rather than use Linux's address or PMM RAM.
3. **Late owned capture, one-use mapping boundary.** Keep uACPI on its existing
   BSP worker, which returns owned immutable root/consumer interval data. Before
   AP startup, reserve a claim's configuration authority and a bounded dedicated
   native virtual window. After ACPI capture, permit its BSP owner to assign a
   still-fenced BAR and map previously unpublished addresses once; no borrowing
   the ACPI window, remapping or runtime reuse. Failed mapping slots stay
   consumed. This extends the boot-only PCI rule explicitly, following the
   existing fresh-address mapping invariant. PCI owns the allocation/ledger;
   the architecture adapter owns ECAM and cache/translation details, and kernel
   VM owns page tables. The larger alternative is reordering ACPI initialization
   before AP startup; a platform address override is not an alternative.

First implement read-only aperture/exclusion capture and inspect the resulting
allocation proof; only then wire selected-BAR programming and the LPSS consumer.
Each successful assignment logs root/BDF/BAR, bus/physical range and size once;
refusals identify the missing authority, route, extent or readback. No per-I/O
chatter. **After this slice the owner can:** see a formerly unassigned claimed
BAR placed without moving another device, then retry vela's HID descriptor read.

Qualification: ordinary builds, interactive QEMU/GDB on 1/4 CPUs, existing
storage/network/input controls and inspection of masks, interval ownership,
decode/BME and failure retention. QEMU evidence must exercise a real initially
unassigned BAR; otherwise label that path unqualified. No new emulator or probe
infrastructure is authorized by this proposal. Native vela uses the normal menu
entry and persistent `home://` output per RUNSHEETS; no fixed-address guesses.

Reference: [Linux ACPI host-bridge resource guidance](https://www.kernel.org/doc/html/latest/PCI/acpi-info.html)
distinguishes apertures from consumers and explains PNP0C02 reservations. Older
Word/DWord/QWord descriptors cannot use the Producer/Consumer flag alone to
classify host registers; collect explicit reservations as well. Unsupported
translation/attributes or incomplete evidence fail closed.
