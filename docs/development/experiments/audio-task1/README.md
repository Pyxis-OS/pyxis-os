# Audio task 1: contracts and controller/codec bring-up

In progress, owner assignment **2026-10-08**. [#549](https://git.internal/PyxisOS/pyxis-os/pulls/549)
merged into fresh main `ad0db38`; task branch `audio/controller-codec` is rebased
onto it. The earlier baseline from `abbeded` has the same kernel, ABI and pinned
consumer inputs; #549 added documentation only. Controller/codec bring-up with
BSP-owned DMA is assigned, QEMU first, with native checks in the later batch.
Public grants/sessions/mixing, IRQ refill and consumers remain later tasks.

## Accepted task-specific decisions

Accepted by the owner through the orchestrator on **2026-10-08**, before code:

1. Document ACQUIRE/WRITE/STATUS/RELEASE using native message headers; WRITE
   carries a caller address/byte count and STATUS reports queue capacity and
   discontinuities. Implement only the private engine now, public calls/sessions
   in their later task.
2. Copied nonblocking all-or-nothing WRITE, at most 4096 bytes aligned to four-byte
   stereo frames; full queue returns CALL_WOULD_BLOCK with no acceptance.
   WAIT_WRITABLE guarantees space for a maximum write. No drain promise or
   retained caller buffer.
3. Existing errors: CALL_UNAVAILABLE for absent/unsupported/failed hardware,
   CALL_BUSY for an acquired session, CALL_LIMIT for the ninth active session,
   CALL_NO_MEMORY on allocation, CALL_DENIED for missing authority/wrong owner.

The [accepted contract](../../../wip/hda-playback.md#accepted-session-call-contract)
documents the intended layouts and behavior without exporting placeholder ABI.
No new owner decision is needed to begin the assigned private engine.

## No-audio baseline

Captured before implementation, from clean main `abbededa6a` (includes merged
pointer #545). Ordinary `make -j16 image` passed in the existing LLVM
23.1.3/49e2c1a builder. Pinned sources were rebuilt, including the new pointer
userland/ports revisions; no stale pre-pointer bundles were used. Vendored
sbase/Quake sources emit existing warnings; no warning-free whole-build claim.
Provenance: [kernel](baseline-kernel.txt), [SDK](baseline-sdk.txt),
[userland](baseline-userspace.txt), [ports](baseline-ports.txt),
[configuration](baseline-kernel.config).

| Artifact | SHA-256 |
| --- | --- |
| ELF | `9b08b75e01d87de30e560e0aeac7de27c2982cf1e80e1f419145752ecbbcfee7` |
| ISO | `fbd04c4259770460a7a20ad76ab52968eefe2b03563607365d961aa81e11eb07` |
| Initrd | `3dbc113e9cd4df7111ffdfa1eb4a31cd5549f1abcedc1f26509133062a28830c` |

Stock QEMU 10.2.2, Q35, `-cpu max`, four cores/one thread each, 8 GiB, nested KVM,
fresh OVMF variables, default VGA 1280x800. Caelum selected, static TTY cursor,
all three user spaces/network ready; no remote client connected during the idle
cost window. Modern VirtIO SCSI read-only CD-ROM, NIC and RNG; `intel-hda` plus
`hda-output` and WAV backend (48 kHz S16 stereo) present but no HDA driver/audio
consumer. Command shape matches the [investigation](../audio-investigation/README.md#guest-configuration-and-commands),
with matching baseline ELF/ISO and local filenames. Added only a QEMU pidfile
for host accounting; no guest instrumentation or new boot/output automation.

### Presenter elapsed observations

Eight individually entered hardware-breakpoint/`finish` pairs around
`space_present`, using the matching ELF and `set may-call-functions off`.
HPET counter reads at `0xfffffe80402020f0`, period 10 ns, following the existing
[screenshot timing method](../../screenshot-qualification.md#matched-presenter-cost).
No inferior calls or guest state edits. GDB confirmed `active_space="caelum"`,
1280x800 and the BSP presenter task. [Raw transcript](baseline-presenter-gdb.txt).

Ticks: **72269, 73700, 86171, 85435, 132846, 68946, 85415, 99932**.
Median **0.854250 ms**, range **0.689460–1.328460 ms**. These are debugger-qualified
elapsed observations including preemption/device waits and nested-host variation,
not isolated CPU time or native performance.

### Host cost with idle guest

No debugger attached during this separate window. Two manual `/proc/<pid>/stat`
and thread-stat snapshots, with host monotonic timestamps and `CLK_TCK=100`:
[start](baseline-cpu-start.json), [end](baseline-cpu-end.json). Fields are user
and system CPU ticks, not guest-idle percentages. QEMU monitor `info cpus`
identified CPU0/1/2/3 thread IDs 355643/355644/355645/355646.

Elapsed **74.321553919 s**; QEMU process CPU **14.84 s**, or **19.9673% of one
host CPU**. This includes nested KVM execution/exits and QEMU overhead with the
presenter/timers/network idle, not guest CPU utilization. Per-thread reads are
sequential and jiffy-rounded, so their sums can differ slightly from the process.

| Guest vCPU thread | Host CPU seconds | Percent of one host CPU |
| --- | ---: | ---: |
| CPU0 / BSP | 7.51 | 10.1047% |
| CPU1 | 3.16 | 4.2518% |
| CPU2 | 2.22 | 2.9870% |
| CPU3 | 1.96 | 2.6372% |

Repeat both methods after bring-up with the same configuration, idle selection
and initrd/bundle bytes; report actual window duration and normalize CPU time.
Record engine-idle and any active-playback cost separately. A single window is
not a stable native utilization or regression threshold. All baseline guest,
debugger and build processes stopped. Local baseline bundles were packaged for
matched reuse. No native host was accessed and the ALC257 dump is unchanged.

## Remaining work / handoff

Implement the assigned private controller/codec engine, qualify QEMU command/
routing/known PCM/stop ownership on an unmerged consumer branch, and repeat the
matched idle-cost observations on the production image. Publish a focused
implementation PR with exact revisions and existing CI; update only completed
checklist steps. Do not begin later session/mixing/consumer tasks implicitly.
