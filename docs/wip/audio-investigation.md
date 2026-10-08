# HD Audio investigation

Status: **investigation authorized, 2026-10-08**. Playback only, starting in
QEMU; the deliverables are a report and a production milestone proposal.
The [audio direction](later-os-directions.md#audio) does not settle an interface.
No production driver, audio ABI or consumer implementation is authorized here.

Base: `67e14be`, fresh main after random generator #546 merged. All probe code
stays on separate unmerged branches. The documentation branch is
`docs/audio-investigation`; reports will identify exact probe revisions,
matching artifacts, configuration, observations and limits.

## Numbered investigation steps

1. [ ] **References, ownership and baseline.** Inspect the Intel HDA specification,
   QEMU 10.2.2 controller/codec and current BSP PCI/DMA/scheduler contracts.
   Record a normal guest with HDA present but no probe running, and a small
   comparable responsiveness check. Identify SDL2/Quake backend constraints
   without changing their code.
2. [ ] **Controller and commands.** On an unmerged branch, claim the emulated
   controller, map registers, reset the link, read capabilities and establish
   CORB/RIRB command transport (or record why immediate commands are needed).
   Keep all mutation/DMA on a BSP worker, mask interrupts while polling, bound
   waits and retain backing when hardware ownership is uncertain. Record
   ring progress and command-response correlation.
3. [ ] **Codec and route.** Enumerate codec/function-group/widget capabilities,
   connection lists, PCM/rate support, amplifiers and output pins. Discover a
   converter-to-pin path rather than assuming ThinkPad codec node numbers.
   Record the QEMU route and the controls needed to activate it.
4. [ ] **Known PCM playback.** Program one output stream and a BDL with a
   deliberately bounded known PCM buffer. Capture QEMU's WAV output and inspect
   its format, sample content, elapsed time, stream positions/completions and
   stop/reset behavior. Repeat ordinary guest responsiveness checks. This is
   emulated playback evidence, not a native sound or latency result.
5. [ ] **Native requirements.** Reconcile the checked-in ThinkPad inventory and
   any owner-supplied codec/Linux data. Analog is AMD `1022:15e3`; GPU HDMI/DP
   and dock USB audio are distinct devices. Codec identity, routing, amplifier/
   power quirks and native DMA/interrupt behavior remain unknown unless supplied
   or qualified. Do not access the host reserved for Bluetooth work.
6. [ ] **Report and milestone proposal.** Summarize observations and their limits,
   retain reproducible measurements, and propose focused production tasks for
   ownership/session authority, formats/conversion/mixing, buffering/underrun/
   latency, SDL2 adaptation and native bring-up. Put at most three decisions to
   the owner at once, each with a recommended default. Stop before implementing
   any undecided interface.

## Probe limits

Use `intel-hda` with `hda-output` first; inspect `hda-duplex` separately if useful.
Recording, HDMI/DP, USB audio, codec-specific native fixups and sound consumers
remain outside the probe implementation. No SDL2, pointer or other agents'
changes. A probe's constants are investigation parameters, not accepted ABI or
capacity/latency policy. No new tests, boot automation or CI workflow.

Primary references: [Intel HDA specification](https://www.intel.com/content/dam/www/public/us/en/documents/product-specifications/high-definition-audio-specification.pdf),
[QEMU controller](https://github.com/qemu/qemu/blob/v10.2.2/hw/audio/intel-hda.c),
[QEMU codec](https://github.com/qemu/qemu/blob/v10.2.2/hw/audio/hda-codec.c),
[QEMU audio backends](https://www.qemu.org/docs/master/system/qemu-manpage.html#audio),
[BSP contracts](../kernel/smp.md) and [ThinkPad inventory](../targets/t14-gen1-amd/thinkpad-inventory-undocked.txt).

Native codec dumps and matching `lspci -nnk` were requested from the owner;
they are optional for completing QEMU work. No codec identity, native playback
or production decision is assumed. No task-owned build/guest processes yet.
