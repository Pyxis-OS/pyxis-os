# Intel eDP display driver through IEGD

Assigned to Codex alpha; all four proposal decisions accepted and sequential
implementation authorized (chronium, 2026-11-02). Builds use explicit `-j12`,
at most `-j16`, with one heavy build per agent. Raise new behavior, authority
or lifetime decisions when the relevant task reaches them.

This milestone delivers real display ownership on vela's internal eDP panel.
The current inherited-display implementation remains the interim. Further
[inherited-flip](intel-inherited-scanout.md) work (#1683) and
[two-plane work](intel-two-plane-scaling.md) (#1623) are parked drafts, not
implementation dependencies. GuC rendering is later work.

## Accepted decisions

1. **Native kernel driver and OAL (chronium, 2026-11-02).** One Intel owner holds
   PCI, MMIO, power, GGTT, scanout and IRQ state. The native display adapter lives
   in `kernel/display`; x86-specific OAL/PCI/DMA/cache support lives under
   `arch/x86_64/intel`. Use existing presenter, frame, capture and DISPLAY
   authority. Do not import Linux ioctl/DRM/cdev authority or add a userspace
   hardware provider. Device/firmware execution stays on a fixed BSP owner.
2. **Real eDP target (chronium, 2026-11-02).** Adapt the required MIT xe/i915
   eDP behavior, including panel discovery, power/backlight, link and exact
   timing support. Do not treat external DP, a fixed 1080p fallback or continued
   dependence on Limine's inherited pipe as completion. Preserve software
   composition and llvmpipe; settle exact link/DSC/refresh needs from discovery,
   without promising 120 Hz before qualification. Separate display initialization
   and stop from the render/GuC lifecycle.
3. **Pinned fork (chronium, 2026-11-02).** Use
   [PyxisOS/pyxis-iegd](https://git.internal/PyxisOS/pyxis-iegd), pinned as
   `third_party/iegd`, based on upstream alpha.1 commit
   `db7b38582c955269a38928dac5e36dd1a3998372`. Keep notices and publish separate,
   reviewable commits for native seams, eDP/timing additions and concrete fixes.
   The native kernel adapter stays in pyxis-os. Never follow a floating branch.
4. **Confirmed scanout retirement (chronium, 2026-11-02).** Correct IEGD's
   timeout-based framebuffer release the xe way: actual confirmed replacement
   or disable precedes freeing, with joined IRQ/deferred-work retirement.
   A lost IRQ or elapsed timeout does not establish that hardware stopped
   fetching. Retain possibly-live storage and report failed recovery when its
   retirement is uncertain. Do not panic routinely or claim GOP fallback after
   modesetting unless recovery has re-established valid scanout.

## Reference behavior and present gaps

The fork already contains the exact upstream base on `main`, including its
history and MIT notices. The annotated `v1.0.0-alpha.1` tag resolves to that
same commit. No base rewrite or new import is needed. Upstream's public history
is a scaffolding commit followed by the alpha snapshot (2026-08-27); its runtime
version is `0.9.1-alpha` and its Linux UAPI is explicitly unstable.

`hal/pciids.h` includes vela's `8086:b0a0`. The source implements real initial
modesetting, GGTT/BO management, page flips and vblank processing. However:

- It has no internal-panel power/backlight or VBT/OpRegion implementation;
  its DP path includes external-monitor D3-to-D0 behavior and non-eDP PHY rules.
- Its timing conversion requires a fixed table, lacks 2880x1800 and replaces
  the EDID clock. The Linux `SET_MODE` ioctl only logs and returns success.
- It has no general HPD interrupt/reprobe path. Initial detection is not hotplug.
- Combined device initialization tears down all GOP outputs, initializes engines
  and attempts GuC. Combined teardown resets GT. Neither is a display-only
  lifecycle that can be called unchanged.
- `display_shutdown_scanout()` ignores timed-wait failure before dropping buffer
  references; its comment acknowledges possible continued fetching. Decision4
  authorizes correcting this concrete defect, not speculative driver hardening.

Use the real xe/i915/IEGD implementations before each driver decision. Checks
stricter than the reference or different behavior need a documented hazard and
chronium's explicit approval. In particular, do not infer ownership from random
nonzero GGTT descriptors or add a missing-VBT refusal where Linux has defaults.

## Native service boundary

The existing OAL is a Linux backend, not a portable implementation: it imports
Linux page, VMA, PCI, waitqueue, spinlock and workqueue types, and HAL callsites
still use Linux allocation/page details. Provide only the concrete native
services needed by the admitted display code; no general Linux compatibility
layer or successful unsupported operations.

| Service | Existing boundary and required work |
| --- | --- |
| Allocation/WC mappings | PMM batches and heap exist. Native `device_pages` adds an owned runtime WB/WC page vector for the fixed BSP kernel worker, separate from bootstrap scanout. Its physical vector is not a device address map. No WB/WC aliases or driver-local allocator bypass. |
| DMA/SG | Keep CPU VA, physical frame, GPU VA and device address distinct. Existing identity-DMA proof does not provide translated SG mappings or permission to change firmware IOMMU tables. Decide any required new mapper at that task. |
| PCI/MMIO/MSI | Native claims, BAR mappings and single-vector MSI-X/MSI exist. Keep one device owner and the native BME/claim lifecycle; use the reference's access widths, ordering and posting. |
| Delays/waits | Use native monotonic deadlines and task waits. Separate short hardware polling from sleepable waits; never replace a sleeping mutex with a spinlock held over PHY/AUX work. |
| IRQ/deferred work | Native interrupt entry captures/acknowledges status and wakes the retained owner. Join actual work before releasing it. Linux IRQ-safe spinlocks cannot be cast to Pyxis's non-IRQ-entry spinlocks. |
| Firmware/config | Borrow deliberately admitted immutable assets with truthful lifetime/errors. Firmware naming, licence, protocol and content pins are separate from the source pin. |

Current `display_init` is pre-AP/pre-task; the reference probe is sleepable.
Bootstrap reservation/preparation and runtime worker ownership therefore need
an explicit handoff before driver effects. Task2 settles any new native MM,
DMA or lifetime decisions before implementing them; acceptance of the overall
milestone is not a silent change to kernel contracts.

## Task2 decisions at the native-service boundary

**Decisions5 and6 accepted (chronium, 2026-11-02).** They define the
bootstrap/worker and native runtime-buffer contracts for task2.

**5. Bootstrap-to-worker handoff — accepted: keep boot presentation
running while the fixed BSP Intel worker discovers and prepares the owned
display.** Reserve the device/resources during bootstrap, but do not call the
sleepable probe in pre-AP IF=0 context or stall all userspace behind it. The new
driver's development image selects one Intel owner: the inherited flip writer
does not run alongside it. At takeover, close and drain presenter frame leases
before destructive hardware effects. Publish the new confirmed front/layout
through the existing display generation/resize transaction; retain the previous
layout until that commit. Preparation failure can leave untouched GOP copying;
after effects, recovery must actually establish valid scanout before claiming
fallback. This does not add a public source-resolution or modesetting right.

**6. Runtime buffer support — accepted: a native device-owned page
vector in kernel/mm, with explicit WB or WC CPU mapping and the existing
identity-DMA admission for the held requester.** CPU and device/GPU addresses
remain distinct; this is an internal kernel service, not a userspace constructor
or Linux `struct page` emulation. Prepare/zero/map through the existing PMM,
cache-alias and shared-VM owner rules. GPU-visible publication retains the buffer;
release requires the owner to have joined CPU borrowers and actual device
retirement before unmapping/freeing. An uncertain VM/ACK or device outcome keeps
the owned record/backing and fences reuse, as the existing VM/scanout contract
requires. Do not silently broaden today's boot-only DMA helpers or introduce
an arbitrary framebuffer cap. Allocation failure is reported; no eviction,
IOMMU reprogramming or hidden blocking reserve.

Panic publication and exact hardware recovery are settled when task5 reaches
those transitions, using the owned front and reference behavior; they are not
silently inferred from the old GOP pointer or designed prematurely here.

## Ordered tasks

Implement these sequentially. Each task records its own PRs, exact heads and
appropriate qualification; source, build, QEMU and native results stay distinct.

| Task | Deliverable | Status |
| --- | --- | --- |
| 1. Source foundation | Exact mirrored alpha.1 base/history/tag/notices verified; `third_party/iegd` pinned with the normal main-ancestry check and documented repository/build boundary. Interim source/kernel baseline recorded. No driver objects or hardware activation. | [x] |
| 2. Native preparation and owner | Define the bootstrap-to-worker handoff and concrete OAL contracts, then implement required native MM/WC/DMA, timing, wait and IRQ/work lifetime support in focused PRs. No generic Linux emulation. | [ ] |
| 3. Panel discovery | Reuse retained Fedora and boot evidence; collect missing VBT/OpRegion, EDID/DPCD, route/link, panel control and inherited firmware state through an orchestrator-scheduled read-only packet if needed. Implement Linux's real parsing/defaults. | [ ] |
| 4. Display-only eDP port | Native device/context seam, deliberate display init/stop, exact timings and needed MIT eDP power/link/backlight behavior. Correct timed release and native IRQ/work integration. Keep GuC/engines/render UAPI out. | [ ] |
| 5. Own and present | Reference GGTT reservations/scratch/PAT/binding and mode/pipe/plane sequence, owned CPU-written scanout, vblank-correlated presentation/reuse, safe stop and panic/blank behavior. Retire the old selected-device writer; never run two owners. | [ ] |
| 6. Qualify and activate | QEMU software/OAL and unaffected-driver controls, then separately scheduled vela eDP modeset/presentation, cold/warm/reboot/stop and recovery checks. Attended terminal/OpenArena optical check after native state/completion evidence. Activation is an explicit final checkpoint. | [ ] |

Existing interim evidence includes vela's retained Fedora `card1-eDP-1`,
`8086:b0a0`, and the 2026-11-02 cold capture at 2880x1800. That capture read a
full nonzero GGTT, refused flips and published no new mappings. It is evidence
against zero-descriptor allocation as a contract, not native qualification of
this driver or proof that every descriptor names a live consumer. Baseline
measurements must identify the actual source/configuration and observer; no
new timing result is implied by recording these facts.

### Source-foundation validation

The task1 baseline is source `ee47b765d1d702546e3e66fd10643a8ea4c663f4`
(docs only over main `dda7c7e9b09932264f61b014fa9e572979c6ebd2`), normal kernel
configuration, existing `pyxis-llvm23.1.3-b33ad74-r1` builder and explicit `-j12`.
Kernel build passed. This is a correctness/source baseline, not a timing or
native driver result. Two existing warnings remain in NPFS/lwIP; this task does
not change those subsystems. No new hardware worker, mapping, frame, firmware
load or source/object build rule is added by the pin.

## Code retained and replaced

Keep the common presenter, software composition, tab bar, frame/capture/grant
contracts, idle blanking and independent Renoir/VirtIO/Bochs drivers. GOP and
the early console remain bootstrap output, not a guarantee of visible pixels
after modesetting. Establish an owned panic target for the new display.

On the device taken over by IEGD, replace the interim inherited power/GGTT/PAT/
flip owner; do not preserve its admission restrictions inside the new driver.
Generic VM/PCI/scanout improvements stay where still used. Read-only evidence
tools may survive separately. The parked two-plane/scaling contract is not
implicitly implemented or discarded by bringing up one primary plane.

## Source and firmware supply

Keep original Intel MIT notices and record every adapted source's pin/path and
licence. The fork preserves upstream source/history; an explicit build file
allowlist later admits only the display/OAL subset. Do not compile Linux IAL,
cdev/ioctl/sysfs/PMU/test tools or GuC/render code to satisfy unused includes.
Keep the native hardware adapter private to the kernel. Builds read the pinned
internal fork; no upstream download fallback or compiler rebuild is required.

No GuC blob is part of the initial source foundation. IEGD requests
`iegd/ptl_guc_70.bin` without an exact qualified minor/patch pin. It supplies no
HuC/GSC/DMC loader. Its DMC power assumption is not cold-start independence;
follow Linux's actual DMC/power handling when the panel task reaches it.
Before admitting any blob, record source/version/hash/size, driver compatibility
and installed path, with the complete applicable firmware licence and WHENCE
association. Firmware licences are separate from the MIT source licence.

## References

- [IEGD alpha.1 source](https://github.com/intel/intel-edge-graphics-driver/tree/db7b38582c955269a38928dac5e36dd1a3998372), [release notes](https://github.com/intel/intel-edge-graphics-driver/blob/db7b38582c955269a38928dac5e36dd1a3998372/docs/release-notes.md), [OAL](https://github.com/intel/intel-edge-graphics-driver/blob/db7b38582c955269a38928dac5e36dd1a3998372/oal/oal.h), [flip/retirement](https://github.com/intel/intel-edge-graphics-driver/blob/db7b38582c955269a38928dac5e36dd1a3998372/hal/display/fb.c).
- Linux v7.1 `8cd9520d35a6c38db6567e97dd93b1f11f185dc6`: [eDP initialization](https://github.com/torvalds/linux/blob/8cd9520d35a6c38db6567e97dd93b1f11f185dc6/drivers/gpu/drm/i915/display/intel_dp.c), [BIOS/VBT/defaults](https://github.com/torvalds/linux/blob/8cd9520d35a6c38db6567e97dd93b1f11f185dc6/drivers/gpu/drm/i915/display/intel_bios.c), [PPS](https://github.com/torvalds/linux/blob/8cd9520d35a6c38db6567e97dd93b1f11f185dc6/drivers/gpu/drm/i915/display/intel_pps.c), [backlight](https://github.com/torvalds/linux/blob/8cd9520d35a6c38db6567e97dd93b1f11f185dc6/drivers/gpu/drm/i915/display/intel_backlight.c).
- [Native VM ownership](../kernel/vm-ownership.md), [PCI](../devices/pci.md), [display](../kernel/display.md).

## User-visible completion

Vela boots and presents through its owned eDP driver, independent of Limine's
selected pipe. Terminal/game frames use confirmed synchronized presentation;
uncertain scanout is not freed or disguised as a successful fallback. Applications
keep their existing DISPLAY authority and software renderer. No hardware rendering,
external hotplug or second-plane acceleration is claimed by that completion.
