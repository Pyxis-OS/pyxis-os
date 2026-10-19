# Hands-free native durability measurements

Owner accepted decisions 1–3 for [task 4](../../wip/explicit-durability.md),
2026-10-19. **Implementation/qualification in progress; no native runs yet.** This supersedes the hands-on
packet; it does not authorize PXE staging or media writes. Private host names,
addresses, device identifiers and boot-entry numbers stay in local configuration.

## Accepted decisions (owner, 2026-10-19)

1. **External controller; bounded chunks (accepted default).** Run the controller on the
   LAN host, surviving target reboots; use Fedora only through short SSH steps.
   Measure original/corrected-2b and pre-/post-3, two samples each, alternating
   within each group: eight measured boots. Add a third sample for a state only
   if its two values differ by more than 10% of their mean in any aggregate
   envelope; retain both original observations. Additional samples may wait for
   another chunk. Full five-state/three-sample coverage is optional, not default.
   Apply the regression stop below before adding thirds or advancing groups.
2. **Private replay hook with separate control/payload inputs (accepted default).** Use
   never-merged installer/init changes in both frozen SDK groups, not kernel
   changes. Read an explicitly staged framed plan FILE and execute the existing
   `--apply --confirm=update` path. Generate plans using the same frozen planner
   on disposable, exact-geometry image clones; native revalidation stays intact.
   The two untimed seeds may plan and replay in the same boot as described below.
   No choosing the first disk, weakening hashes or substituting a mismatched plan.
3. **Durable scratch-ESP result, then clean restart (accepted default).** After executor
   completion and ownership cleanup, save the bounded timing/result record to a
   dedicated file on the scratch ESP, synchronize and release it, then restart.
   Fedora reads it without mounting the installed pool. This is the authoritative
   data channel; ordinary UDP logging is optional supporting evidence, not a
   success prerequisite. Result-write or uncertain cleanup failure stops the run.

The owner substituted the **disposable 32 GB SanDisk**, 2026-10-19. Its earlier
[SuperSpeed transfer runs](usb-superspeed-transfers.md#current-abi-integration-after-rebase)
include the re-flashed-stick weekend A/B series. Those measurements provide
hardware context, not timings to pool with this code-state comparison. All native
comparisons here restore the same fixture on that same stick.

## Controller and preflight

The owner supplies the directly attached disposable 32 GB SanDisk USB stick, Fedora as the
stable default during each chunk, Tailscale SSH and passwordless sudo. A local
configuration binds the approved USB identity/capacity, target SSH name, explicit
PXE Boot#### and both reviewed BootOrder lists. Record the original PXE-first
order before changing it; never infer boot entries from their list positions.
Gamma may help with one-time Fedora setup only. Disable automount for this exact
scratch target; never disable storage services globally.

The controller persists an atomic ledger on its own host: sample nonce, variant,
input/fixture/plan hashes, staging acknowledgement, state and observed outcome.
It never resumes an interrupted operation blindly. Resumption first reconciles
Fedora's boot ID, firmware state and any nonce-matched result; an ambiguous
sample stays failed and needs operator review. Only completed sample boundaries
are automatic resume points.

First perform one **untimed, non-writing PXE preflight**: verify Fedora SSH and
sudo, set `efibootmgr --bootnext` to the exact network entry, read it back, and
reboot. A private preflight image reports its identity and requests restart;
Fedora must return with a changed boot ID, the expected BootCurrent and BootNext
consumed. Observe PXE delivery on the staging host too. Failure or fallback to
Fedora without evidence of the Pyxis preflight stops setup. Do not assume this
firmware honors BootNext for network boot until this passes.

Use `BatchMode=yes`, `ConnectTimeout=10`, one connection attempt and an overall
20-second deadline for each SSH health command. Tailscale check-mode approval can
hang after connection: treat an established-session hang as stop-and-report.
While rebooting, bounded connection-refused/unreachable checks are availability
observations, not repeated execution of a restore, BootNext write or reboot.
No SSH key setup is needed. Restore/verification has its own bounded deadline.

An on-target systemd oneshot with a persisted step file could resume after each
boot, but adds startup privileges, automount/service ordering and a second state
machine, and cannot observe Pyxis hangs or stage variants while Fedora is down.
It is not recommended. No persistent target controller/unit is needed.

## Plans without circular source hashes

The current installer parses a framed plan from stdin, checks source/target hashes
and applies explicit consent; explicit consent does **not** request restart.
Its source hash includes the installed archive, and classic replay requires a
valid GPT identity. Putting a per-sample plan into that same archive creates a
self-reference. Do not patch the hasher to exclude it.

Private boot layout: a private outer control archive contains the patched
measurement executables, request/plan files, a frozen source directory and the
unchanged inner payload archive. Private init delegates that directory as `boot`,
the inner FILE as `boot_archive`, and the actual variant ELF as `boot_kernel`.
The measurement executable runs outside the frozen source directory. Neither
control files nor per-sample nonces enter installed programs, rescue payloads or
source hashes. All variants in each caller group use identical frozen payloads.
No new public ABI, native kernel probe or compiler-container rebuild is needed.

The small private hook opens one bounded staged plan FILE, rejects trailing data,
feeds it to the existing parser and retains normal Review/preparation/consent/
revalidation. It bypasses only trusted UI/profile prompts using the manifest's
explicit classic profile. Missing/mismatched inputs fail before effects.

After local Fedora capture, prepare each plan on a disposable reconstructed clone
with the real stick geometry, matching variant ELF and frozen payloads. The
private plan-only mode selects the explicit GPT identity and runs the existing
planner; it neither edits the record nor applies it. Boot-scoped disk IDs are
already canonicalized by the target fingerprint. Transferability still needs
QEMU qualification and the first native revalidation; any mismatch stops without
a fallback to auto-planning during a measured sample.

For the two untimed seeds, use a separate private **prepare-and-apply** mode:
select the manifest's exact GPT identity/capacity, use the existing native planner
with explicit classic/read-the-room/journal choices, format its record in RAM,
then parse/revalidate/apply it with the preapproved `wipe` or `update` consent.
Record that framed seed plan with the result; no measured sample uses this mode.
This avoids a full copy of the stick's arbitrary previous contents or extra
native planning boots. Both seed boots remain untimed, followed by Fedora's
capture/fsck/two-revision/feature-zero checks. A missing/ambiguous GPT identity
stops bootstrap; do not bypass nonportable-plan refusal. If the starting scratch
identity needs preparation, bring that specific change back for review.

## Each sample

1. Require an orchestrator staging acknowledgement naming the exact variant,
   control-archive, ELF, payload and plan hashes. The controller does not modify
   the PXE menu or infer that the expected variant has been staged.
2. SSH Fedora: verify boot ID and target identity; restore the same captured view
   with the existing exclusive, whole-USB helper. Refuse mounted/held/open targets,
   capacity changes or hash mismatches. Hold the claim throughout restore and
   verification; no automount between release and reboot. Unreachable free data
   is excluded as in the existing fixture contract, not called whole-disk erasure.
3. Set/read back BootNext once, save the transition in the off-target ledger, then
   reboot once. Wait for Pyxis evidence and eventual Fedora return with deadlines.
4. The private installer applies the staged plan, retains the existing clock
   envelopes and checks cleanup errors as well as execute success. After all
   original cleanup, write/sync a nonce-matched result to the scratch ESP, release
   it and request the normal flushing restart. Result I/O is outside the measured
   envelopes and labelled separately; it adds no work to their hot paths.
5. On Fedora return, read the result under a read-only device claim, verify all
   identities/statuses, fsck/content/layout and exact retained revisions using
   stopped host tools, then persist acceptance. A stale/missing result, failed
   cleanup or nonzero status is not a timing sample. Collect before restoring.

Plan/result files are data, not authentication or additional authority. The
existing grants and exact target revalidation remain decisive. UDP is lossy and
normal Install does not configure IPv4; `log.udp=1` alone is insufficient. Do not
add a network owner or rely on Remote just to obtain these results. Native PXE
and Fedora/result observations supply liveness; optional beacon/UDP requires a
separately qualified network profile. Before a usable ESP exists, failure may
leave no result file; timeout/missing result still stops the controller.

The disk record says **prepared**, not that its own last close/retirement has
completed. `cleanup_status=ok` covers the original installer cleanup before the
result write. The guest requests restart only after result sync and result-view
retirement succeed. Acceptance additionally requires the live controller's
uninterrupted wait, a fresh Fedora boot and clean stopped filesystems. Timeout,
controller interruption or manual recovery invalidates the attempt; `resume`
never promotes a prepared record into an accepted sample. Unexpected power/reset
indistinguishable from the expected reboot is an external-run limitation; nobody
may manually reset the target during a running sample. No recursive on-disk
"last close succeeded" marker is claimed.

## Time and clean stopping

Budget **6–8 minutes per measured sample**, including restore/readback (~1–2),
PXE/Pyxis and Fedora startup (~3–4), executor/result checks (~1–2). These are
planning allowances, not measured native durations. Eight samples cost roughly
48–64 minutes. Allow another 15–25 minutes for preflight, two automated seed
boots, capture and local clone planning: **roughly 65–90 minutes end to end**.
Transfer/build packets before starting the occupied-target interval. If preflight
shows the estimate is wrong, stop/reduce the chunk; never silently extend it.
Extraction is not hidden in this budget or claimed measured by Update.

**Early regression stop required by the owner, 2026-10-19.** After two samples
per state in a comparison, evaluate each of the three matched envelopes before
any third sample or next group. Accepted threshold: both newer observations
exceed the predecessor maximum and the newer median is at least **5% higher**,
or its median is at least **20% higher** despite overlapping ranges. Stop the
matrix on either condition; retain/report all observations and restore the normal
PXE payload/default as below. This is a conservative scheduling trigger, not
proof of causal attribution from two samples. Do not compare across SDK groups.
When a state's two samples agree within 10% of their mean, skip its third sample.
Other third samples need time remaining in the chunk; no unconditional fifteen-
sample run after a regression is evident.

Default deadlines: 5 minutes per restore or local validation step, 10 minutes
from reboot request to verified Fedora return, 45 minutes per scheduled chunk. Record each
phase separately. A hung sample stops the controller: no blind retry, forced
reboot, power cycle or restore. Preserve evidence and ask the orchestrator; a
machine stuck in Pyxis may need recovery before firmware can be restored.

Stop requests take effect after a fully collected sample. Before restoring the
PXE-first order, require the orchestrator to restore and acknowledge the normal
non-measurement PXE payload and unchanged menu; never leave auto-apply armed as
the ordinary default. Do not start a sample
without 12 minutes remaining before the chunk deadline or the owner's **20:00**
wind-down, with an additional 10-minute finalization reserve. At a clean boundary,
SSH Fedora to clear any remaining BootNext and restore/read back the owner's
saved PXE-first BootOrder; leave Fedora running. The next chunk explicitly
re-enters the temporary Fedora-first order. If Fedora is unavailable or firmware
writes fail, report that restoration is incomplete; never claim it succeeded.
The owner can choose to perform final firmware restoration instead.

## Validation before native authorization

After acceptance: build both private caller groups; QEMU-check plan transport,
source immutability, success/result/restart and definite-failure stops. Check
result/nonce mismatch and interrupted-ledger handling without physical media.
Then obtain the orchestrator's Wi-Fi-priority gap/staging schedule and qualify BootNext on the real
firmware before any seed or measured write. Current QEMU repeat records remain
separate; this proposal is not evidence that the harness or native run passed.
