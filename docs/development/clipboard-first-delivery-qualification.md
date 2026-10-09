# Clipboard first-delivery qualification

Baseline captured 2026-10-09 before implementation at Pyxis `c1e5f3be`,
userland `72f303d6`, ports `8b918c46`; compiler image
`pyxis-llvm23.1.3-49e2c1a`. The ordinary `make -j16 image` build passed.
The accepted contract is in [the clipboard milestone](../wip/clipboard.md).

QEMU 10.2.2: Q35, nested KVM, `-cpu max`, four CPUs (one socket, four cores,
one thread), 512 MiB, UTC RTC, fresh raw OVMF, modern VirtIO GPU/SCSI CD/RNG,
relative PS/2, no NIC/disk/USB/audio, `-display none`, 1280x800. For matching
runs only, staged `config/live.lua` adds a `mux` space using `init-readonly`,
with `launch`, `multiplexer`, `screenshot` and read-write RAM `home`. The
configuration is local qualification data; the shipped profile is unchanged.

Manual HMP input with read-only GDB (`set may-call-functions off`) measured four
`space_present` entry-to-return intervals per workload. Existing HPET ticks at
`0xfffffe80402020f0` are 10 ns. Commands: hardware breakpoint, read tick into
GDB convenience variable, `finish`, subtract ticks, continue. These include
debugger/scheduler variation and do not measure native input/paste latency.

| Workload | Baseline samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Caelum idle | 1.99936, 1.59899, 1.62924, 1.48254 | 1.61411 | 1.48254–1.99936 |
| Caelum completed selection | 2.01465, 1.41075, 2.09701, 2.07252 | 2.04359 | 1.41075–2.09701 |
| Local shell idle | 1.69770, 1.97172, 1.64445, 2.03110 | 1.83471 | 1.64445–2.03110 |
| Mux one pane idle | 2.03695, 1.24358, 1.29716, 1.51210 | 1.40463 | 1.24358–2.03695 |
| Mux completed selection | 2.21552, 1.29504, 1.35677, 1.56633 | 1.46155 | 1.29504–2.21552 |

Caelum selection used first-row cells 1–13; local shell was idle; mux selection
highlighted its prompt at content row 0, columns 0–6. Input ran while the VM
executed. Raw logs, captures and baseline ISO/kernel/configuration hashes stay
local under `build/clipboard-baseline`; no test programs or automation were
added. All baseline QEMU/GDB processes were stopped before code changes.

The earlier baseline on `6df2bdd8` predates the fixed click rule and is not used
as this delivery's matched comparison. Main includes the merged correction;
the owner also confirmed it natively in boot log, raw terminals and mux.
Implementation and matched qualification results follow.

## Matched after samples

At Pyxis `4379e011` plus userland `12c52ff`, the same five workloads and local
configuration were sampled before starting the independent functional VM.
No additional VM was running during these measurements. The earlier boot with
invalid store initialization produced no samples and is excluded. Subsequent
focus cancellation glue changes no sampled idle/selection execution path.

| Workload | After samples (ms) | Median (ms) | Range (ms) |
| --- | --- | --- | --- |
| Caelum idle | 1.52351, 1.71927, 2.01727, 1.56174 | 1.64051 | 1.52351–2.01727 |
| Caelum completed selection | 1.35648, 1.83910, 2.23388, 1.11948 | 1.59779 | 1.11948–2.23388 |
| Local shell idle | 6.05271, 1.87170, 1.18207, 1.35912 | 1.61541 | 1.18207–6.05271 |
| Mux one pane idle | 1.51884, 1.10342, 1.14425, 1.19362 | 1.16893 | 1.10342–1.51884 |
| Mux completed selection | 1.51728, 1.46968, 1.20093, 2.53978 | 1.49348 | 1.20093–2.53978 |

Ranges overlap the baseline for every workload. The local-shell series retains
its 6.05 ms outlier; these nested/debugger intervals establish no native latency
claim or speedup. No persistent presentation-cost regression is demonstrated by
these small repeated samples.

## Functional qualification

Manual local-TTY checks used `4379e011` / userland `12c52ff`; mux routing,
first drag after a pane-focus change and larger text used `8e74afa4` / userland
`536d6dc`. Same VM/device configuration as above, existing shell/Lua/vi consumers,
manual HMP input and read-only debugger inspection. Functional VMs could overlap;
no timing result was collected during that overlap. Logs/captures stay local.

| Checked behavior | Observed result |
| --- | --- |
| Selection without Copy; click-cleared/missing selection Copy | No implicit publication; refusal preserves the previous item |
| Local and shared ASCII Copy | Exact owned bytes in distinct layer objects |
| Shared Paste after source space becomes inactive | Destination receives shared text; its empty local layer refuses without fallback |
| Empty trimmed selection | Empty current item; Paste finishes without submitting |
| Multiline local Paste | `echo alpha\nalpha` becomes editable `echo alpha alpha`; a later fresh Enter executes one command |
| Raw vi destination | Refused with no data/frame insertion; a new shell receiver epoch appears afterward |
| Mux selected pane differs from keyboard-focused pane | Copy freezes `echo left` in the selected pane; Paste inserts it into the other focused pane without submission |
| First click-and-drag into an unfocused pane | Creates the intended selection after focus-only cancellation stopped resetting spatial input |
| Mux pending Ctrl+B prefix and history view | Paste refuses; no later insertion when returning live |
| Larger mux selection | 1600 X glyphs over 20 physical rows export 1619 bytes, including 19 LF separators |
| Short records and line capacity | Larger item completes multiple short DATA records; line limit is reported, framing completes, and shell refuses execution even after explicit Enter |
| Selected source pane closes/exits | Both retained 1619-byte layer items survive; shared Paste into the surviving pane still completes |

At larger Copy commit, debugger inspection saw 1628 charged bytes: old current
9 plus staged 1619. After replacement/reaping it saw 1619; independent shared
publication raised retained payload to 3238. Paste references did not duplicate
the retained item charge, and completed transactions released their references.
These are measured example peaks, not maximum-budget stress qualification.

Authority/identity matching, competing receiver registration/reads, stopped
receiver invalidation, 64 KiB/8 MiB admission guards and five-second deadlines
were source-reviewed. No new test program, injected failure or guest function/
memory mutation was used to force their extremes. No native clipboard check is
claimed. A pre-admission Return entered while GDB stopped the VM exposed the need
for a bounded controller/decoder fence; deferred host events not yet observable
by emulated PS/2 cannot prove physical ordering. Final fence checks are recorded
with their tested revision below.

## Integrated image and input boundary

The ordinary full image passed at Pyxis `af1fbf08`, userland `49b3817f`, ports
`bc04b447`, fs `b427df29` and lwIP `a1aadb91`, with the same compiler image.
This includes main's process-lifetime and HTTP changes, the focus-only controller
cancellation fix and the bounded PS/2/decoder admission/resume fence. All ports
were rebuilt against the changed SDK; no older bundle substituted changed inputs.

The same matched local mux configuration booted that source in QEMU. Final
functional checks added a QMP monitor for manual key down/up events; no timing
samples used it. GDB remained read-only. Observed:

- Local Copy retained the exact nine bytes `echo held`. A live QMP Return press
  executed the original typed command. With Return still physically held, Paste
  inserted the copied command without execution; a further Return-down repeat
  still did not submit it. GDB showed both physical Enter-down and quarantine
  set, with the transaction completed. Release followed by a fresh press
  executed exactly once and cleared quarantine.
- The existing Lua CLI printed `string.char(128)`. Retained local TTY cell 960
  was byte 128. Selecting it and issuing Copy refused as unsupported selection;
  the prior nine-byte item charge remained unchanged. No lossy ASCII fallback.
- Mux copied seven selected prompt bytes `ome://>` and pasted them into the
  editable line, without submission. Repeating Paste while Ctrl+B's prefix was
  pending displayed `Paste refused; finish pending input` and inserted nothing.

Local artifacts are under `build/clipboard-final`: ISO/kernel, serial/debugger
logs and captures of held-Enter Paste, fresh Enter, non-ASCII output, mux Paste
and prefix refusal. These final checks do not expand the source-reviewed cases
listed above into runtime qualification, nor establish native latency. All
task-owned QEMU, QMP and debugger processes were stopped afterward.
