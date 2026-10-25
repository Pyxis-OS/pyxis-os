# SoH task 6: gameplay qualification in progress

Baseline source: merged main `fc69c6cd`, userland `88872fe`, ports `e6c51d9`;
the exact task-5 private target/kernel image was reused before task-6 edits.
Nested Q35/KVM, four CPUs, 4 GiB, 1280×800 VirtIO GPU, Intel HDA/WAV 48 kHz
stereo, GL3.3 compatibility/softpipe, 320×240 internal 4:3 and MSAA off.
No native run or performance result is claimed.

Before edits, `boot://pcm.pxe 440 220 10` completed with three starvation
observations and one discontinuity; serial records one commit-clock restart.
The game accepted upstream Space/Start and X/A bindings, opened file selection
and created a slot in its RAM-backed state. This is not reboot persistence.
Keyboard defaults already initialize through `osContInit`; the task-5 Return
attempt used an unbound key, not missing controller initialization.

The longer default-SDL run continued through recoverable horizon/commit faults,
then stopped with the generic unstable-position reason. GDB was attached only
**after** engine shutdown: `start_failure = "LPIB unstable"`, last reads
1124→1324, no recorded FIFO/descriptor errors, completed status set. Both reads
are in the ring and frame aligned. RUN was cached from an earlier observation;
this does not establish RUN at the final failed scan. The current contract
makes exhausted stable-position sampling terminal. The
[accepted task-6 decision](../../wip/ship-of-harkinian.md#task-6-hda-decision-accepted-chronium-2026-10-25)
requires fresh state/error validation before any recoverable classification
(chronium, 2026-10-25). The implementation retains three attempts and the
existing recovery budget, with no inferred progress from a moving pair.

Source inspection also found QEMU aging a completion with a timestamp taken
before the status read that can create that completion. An unsigned age can
therefore underflow. The ordinary correction samples time after the scan and
includes both scans in the commit interval, without raising any limit. Kernel
build passes; exact-head CI #4418 passes all five jobs. The matched
after-run reused the task-5 P1F/initrd and changed only the kernel. The
ten-second PCM result remains three starvation observations and one
discontinuity, matching the baseline. The subsequent SDL game stayed live
through about twenty minutes of wall time and quit normally. Parked GDB
inspection found the engine available, stream stopped and sticky stream errors
zero; 450 bounded recoveries were recorded across both workloads. This is
sustained liveness evidence, not smooth or audible native output. The rare
movement-only branch was not separately observed in the rate-limited log. Specific observation
refusals now retain the controller's reason instead of only the generic label.

A normal mirror-only LUS build passes with keyboard reset delivered to every
binding and SDL rumble defaults restricted to the requested SDL device type.
The previous short-circuit could leave a second held direction pressed on focus
loss. In a separate explicit-Null diagnostic run, GDB observed W+D as stick
69/68. After Super+Right, both releases while hidden and Super+Left, the next
normal controller read was 0/0 with no buttons. No active audio was paused for
that inspection.

Private evidence, target work and baseline WAV/logs stay under
`/shared/oot/soh-9.3.1-task6/`. The private save-completion implementation now
builds: the owner captures
immutable JSON, the real worker persists it, and owner-frame completion gates
success UI/hooks. Failed-before-replace and publication-uncertain outcomes are
distinct; shutdown drains workers without waiting for UI acknowledgement.
The shared LUS configuration path also retains failures and contains its C
bridge, with file and parent sync before success. Private options UI checks
both game settings and configuration outcomes before acknowledging them.
Compiler and source review are not runtime persistence evidence. The disposable
regular-file npfs home fixture is prepared. Short-route play, focus/reset,
rebooted save/reload and refused/full destinations are not yet
qualified. Task 6 remains unchecked.
