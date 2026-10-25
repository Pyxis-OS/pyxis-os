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
[proposed task-6 decision](../../wip/ship-of-harkinian.md#task-6-decision-proposed)
requires fresh state/error validation before any recoverable classification.

Source inspection also found QEMU aging a completion with a timestamp taken
before the status read that can create that completion. An unsigned age can
therefore underflow. The ordinary correction samples time after the scan and
includes both scans in the commit interval, without raising any limit. Kernel
build passes; after-run qualification remains pending. Specific observation
refusals now retain the controller's reason instead of only the generic label.

A normal mirror-only LUS build passes with keyboard reset delivered to every
binding and SDL rumble defaults restricted to the requested SDL device type.
The previous short-circuit could leave a second held direction pressed on focus
loss. Runtime focus checks remain pending.

Private evidence, target work and baseline WAV/logs stay under
`/shared/oot/soh-9.3.1-task6/`. An isolated save-completion implementation is an
explicitly **incomplete, unapplied** private patch; success UI currently still
needs owner-thread completion/error plumbing. The disposable regular-file npfs
home fixture is prepared, not yet booted. Sustained audio, short-route play,
focus/reset, rebooted save/reload and refused/full destinations are not yet
qualified. Task 6 remains unchecked.
