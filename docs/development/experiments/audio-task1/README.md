# Audio task 1: contracts and controller/codec bring-up

In progress, owner assignment **2026-10-08**. The accepted-decision update is
pushed to [#549](https://git.internal/PyxisOS/pyxis-os/pulls/549), head
`a318270feb`, with existing workflow #1295 successful (build 1m1s, filesystem
12s). Three defaults are accepted; no deferred task-specific choice is accepted
by this assignment. The owner assigns controller/codec bring-up with a BSP
worker owning DMA, QEMU first; native checks belong to the later ThinkPad batch.

Task branch: `audio/controller-codec`, based on main `abbeded` and stacked on
#549 while it is open. **Merge order: #549 first, task 1 second.** Rebase onto
merged main before submitting the implementation. The proposal's contract review
and controller/codec engine steps form this assigned bring-up scope; per-space
session/mixer implementation, IRQ/refill work and consumer changes stay in later
tasks. No implementation code has changed yet.

## Pending task-specific decisions

The owner has been asked these three questions before code changes, each with a
recommended default. They remain pending; elapsed time is not acceptance.

1. Document ACQUIRE/WRITE/STATUS/RELEASE using native message headers; WRITE
   carries a caller address/byte count and STATUS reports queue capacity and
   discontinuities. Implement only the private engine now, public calls/sessions
   in their later task. Alternative: implement public calls in this task.
2. Copied nonblocking all-or-nothing WRITE, at most 4096 bytes aligned to four-byte
   stereo frames; full queue returns WOULD_BLOCK with no acceptance. WRITABLE
   guarantees space for a maximum write. No drain promise or retained caller
   buffer. Alternatives: partial writes, or blocking writes with deadlines.
3. Existing errors: UNAVAILABLE for absent/unsupported/failed hardware, BUSY for
   an acquired session, LIMIT for eight active sessions, NO_MEMORY on allocation,
   DENIED for missing authority/wrong owner. Alternative: BUSY for the global cap.

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

Wait for the three contract decisions before changing code. Check #549 merge
state, then rebase the stack as appropriate. Implement the assigned private
controller/codec engine, qualify QEMU command/routing/known PCM/stop ownership,
and repeat the matched idle-cost observations. Publish a focused implementation
PR with exact revisions and existing CI; update only completed checklist steps.
Do not begin later session/mixing/consumer tasks implicitly.
