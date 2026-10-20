# Hands-free native durability measurements

Owner accepted decisions 1–3 for [task 4](../../wip/explicit-durability.md),
2026-10-19. **Native seeds qualified; repeated measured comparisons remain incomplete.**
This supersedes the hands-on packet; it does not authorize PXE staging or media writes. Private host names,
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

The owner confirmed the **32 GB SanDisk is disposable and may be erased**,
2026-10-19. Its earlier
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
consumed. Bind hash-verified source staging to a fresh runtime beacon and nonce
echo; collect TFTP sent logs only if available without an owner approval. The
accepted retry uses the source/runtime binding, not a claim to have captured
those logs. Failure or fallback to Fedora without evidence of the Pyxis
preflight stops setup. Do not assume this
firmware honors BootNext for network boot until this passes.

Use `BatchMode=yes`, `ConnectTimeout=10`, one connection attempt and an overall
20-second deadline for each SSH health command. Tailscale check-mode approval can
hang after connection: treat an established-session hang as stop-and-report.
While rebooting, bounded connection-refused/unreachable checks are availability
observations, not repeated execution of a restore, BootNext write or reboot.
No SSH key setup is needed. SSH runs only from horse to Fedora; the source VM
coordinates staging through the shared directory and needs no SSH to horse.
The campaign runs no sudo on horse. Restore/verification has its own bounded
deadline on the passwordless-sudo target.

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
already canonicalized by the target fingerprint. Clone portability passed in QEMU; the first native revalidation remains
a prerequisite; any mismatch stops without
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

Default deadlines remain 5 minutes per restore or local validation step and
10 minutes from reboot request to verified Fedora return. The original scheduled
chunk was 45 minutes. **Owner exception, 2026-10-19:** the evening campaign may
run from about 19:15 until **23:00**, with cleanup complete inside that bound.
The owner boots Fedora through F12 once; all later boot-order/BootNext and test
steps use the off-target controller without further owner action. The private
runner admits each phase only with its worst-case allowance plus ten minutes
for cleanup. Measured-sample admission reserves five minutes for restore, ten
for boot/return and five for collection; staging is separately bounded. Record
each phase separately. A hung sample stops the controller: no blind retry, forced
reboot, power cycle or restore. Preserve evidence and ask the orchestrator; a
machine stuck in Pyxis may need recovery before firmware can be restored.

Stop requests take effect after a fully collected sample. Before restoring the
PXE-first order, require the orchestrator to restore and acknowledge the normal
non-measurement PXE payload and unchanged menu; never leave auto-apply armed as
the ordinary default. The evening exception replaces the earlier 20:00 cutoff
for this campaign only. Do not start another phase if its allowance would enter
the final ten minutes; stop earlier when the matrix completes or a regression
threshold is met. At a clean boundary,
SSH Fedora to clear any remaining BootNext and restore/read back the owner's
saved PXE-first BootOrder; leave Fedora running. The next chunk explicitly
re-enters the temporary Fedora-first order. With the reviewed
`allow_order_change` setting, the controller performs both transitions through
Fedora passwordless sudo; the owner need not edit firmware order. Handoff must
already be Fedora, or have a separately arranged BootNext to Fedora: the native
Pyxis power API cannot select a firmware boot entry. If Fedora is unavailable or firmware
writes fail, report that restoration is incomplete; never claim it succeeded.
The owner can choose to perform final firmware restoration instead.

## Qualification and remaining native gate

Both private caller groups build with the existing container. Nested QEMU uses
q35/KVM, four vCPUs, 4 GiB, OVMF, VirtIO block/net/RNG and a disposable
32,000,000,000-byte regular-file target; this is functional qualification, not
native performance evidence. The two automated seeds completed, restarted and
passed stopped FAT/npfs checks, matching headers, zero feature masks and exact
revision sets. Capture and restore verified every captured live byte.

Clone-generated plans for corrected task 2b and task 3 made zero target writes.
Both final replays completed and restarted; stopped inspection verified every
program hash (2,240 earlier / 2,246 later), boot ELF, layout, retained revisions
and the current prepared result protocol. Trailing plan data
and repeating seed-update after the second seed both failed without target
writes, result publication or restart. A RAM-only preflight used the real Remote
client, echoed the nonce, requested clean restart and exited QEMU normally.

The off-target controller's 71 local checks cover scheduling, stale/nonmatching
results, interrupted ledgers, refusal paths and clean boundaries using a fake
transport; they do not qualify SSH, native firmware or USB. Read-only copied-tool,
loader, FUSE and checksum checks precede firmware mutations. Private patches and
hashes, artifacts and qualification records are under
`/shared/present/durability-handsfree/`; no private hook enters production pins.

On the ThinkPad (orchestrator/owner, 2026-10-19), Fedora read-only tool, loader,
FUSE, checksum and exact-device checks passed. BootNext reached the staged
RAM-only Pyxis preflight. Automated nonce proof failed while configured for TCP
22379 rather than the known-working 2323 and an inadequate five-minute limit. Status 17 was
`TIMED_OUT`; a firewall cause is plausible, not independently established.
Manual Remote on port 2323 connected immediately to the expected beacon and
requested a normal reboot. Fedora returned, but that does **not** accept the
failed automated preflight. Its ledger and consumed nonce remain unchanged.
Explicit finalization restored the saved normal PXE menu byte-for-byte, read back
PXE-first firmware order and left Fedora running. No native seed, fixture or
measured sample was created.

The next campaign is separate: fresh ledger/nonces, explicit installed Remote
client, port 2323, and an eight-minute maximum proof within the existing
ten-minute reboot-to-Fedora bound. A further 54 local checks qualify bounded
proof diagnostics/refusals. A real reverse-Remote nested-QEMU check passed
nonce echo and normal restart; its forwarded unicast discovery is not native
LAN/firewall qualification. Private corrected copies and the failed-attempt
record are under `/shared/present/durability-handsfree-run/`.

The second campaign, `/shared/present/durability-handsfree-chunk2/`, did execute
its native preflight: Remote became ready, echoed the matching nonce and was sent
the reboot command; Fedora returned with a new boot identity. The helper still
failed at its deadline because it required the host TCP socket to reach EOF.
Native reset need not close that socket; the QEMU `-no-reboot` exit had closed
its host-side virtual connection and hidden the assumption. At 19:07 the
controller restored its saved current PXE menu, verified PXE-first/empty BootNext
and left Fedora running. No seed or measured sample started. This second failed
ledger/nonce also remains unaccepted and unchanged.

The third campaign is `/shared/present/durability-handsfree-retry/`, with a new
ledger, nonces and one-use runtime beacon. The helper retains its client while
the controller checks for a changed Fedora boot identity, expected BootCurrent,
consumed BootNext and the reviewed Fedora-first order. Only after that check
may it deliberately stop its private helper/client group; TCP EOF is not a
return condition. Matching echo/request alone cannot accept a boot. Unrelated
helper errors and stale boot identities still fail. The owner waived privileged
TFTP-log collection; the acknowledgement records source/runtime binding and
explicitly states that no TFTP sent records were collected.

Only an explicit scheduled go starts the retry. It snapshots the current normal
PXE default, automates preflight/seeds/capture/zero-write clone plans and measured
samples where the budget permits, then restores that exact default plus PXE-first
order. Ten minutes remain reserved for cleanup; an incomplete phase is never
retried or accepted. Conditional thirds stay bounded; the 23:00 deadline and
early-stop rules take precedence over finishing the matrix. Local checks use
fake transports/temporary files, not native timing evidence.
Native timings, remaining repeated QEMU performance samples and milestone
completion remain pending.

### Third campaign: seeds accepted, first restore failed

On 2026-10-19, the corrected native preflight passed at 19:26. Both untimed
seed boots then passed their clean-return and filesystem checks. Capture of the
legacy-feature fixture passed, and its immutable copy reached the controller.
The original-state clone plan qualified in QEMU with zero target writes.

The first measured sample stopped during fixture restoration, before BootNext
or a measured Pyxis boot. Its remote child exited 1 after 41.396 seconds at
19:38:15. No measured sample is accepted. The SSH request reached the Fedora
agent and launched the restore helper; the saved error therefore does not
establish authentication expiry as the cause. The agent discarded the child's
captured diagnostic when formatting its exception. Native-only GPT operations,
I/O/verification and partition reread remain unclassified candidates; elapsed
time does not establish how far restoration progressed.

The controller restored its exact normal PXE source/menu, but refused automatic
firmware finalization because the disk mutation was uncertain. The orchestrator
reported Fedora running with Fedora-first order. A separate, explicit
firmware-only recovery checks the same Fedora boot and restored source, clears
BootNext, restores PXE-first and verifies read-back; it never retries the disk
write or accepts the failed sample. Preserve the original ledger and nonce,
with closure recorded in the linked recovery ledger. The orchestrator ran recovery at 19:51: the same Fedora boot, PXE-first,
empty BootNext and original source/menu were verified. The original failed ledger
remains byte-identical; its separate recovery ledger closed at a clean boundary.

The subsequent read-only comparison matched all 888,394,240 captured live bytes
and their recorded digest. No relevant kernel messages or stale restore process
were recorded. This proves the current fixture bytes, not the failed helper
stage: the target already matched the captured fixture before restoration. No
specific failure cause is established, and no measured resume is prepared. The
two validated seeds and immutable fixture remain available for a future reviewed
fresh ledger; neither authorizes replaying the failed operation. Ten local fake-transport checks cover firmware-only closure and
refusal for changed PXE source, boot identity or a pending BootNext. These checks
are not native recovery evidence.

A separate diagnostic-only agent fix is prepared under the campaign's
`diagnostic-fix/`, with patch, checksums and seven passing local logging checks.
It preserves the restore child's complete combined stdout/stderr in a private,
unique Fedora log, and includes its path and bounded tail in a nonzero-exit
error. The agent source digest is
`96d6bd13b112ea2aeaece0bdde5a09990e1b513aa5b89e873599a4fcb55caa12`.
It has not been installed or run natively; the frozen kit/config and previous
ledgers are unchanged. It repairs missing diagnostics, not an attributed restore
failure. The campaign is parked at the verified clean boundary, with Fedora
running and no forced reboot or destructive retry.

### Accepted measured resume (owner, 2026-10-20)

The owner authorized a new scheduled window with cleanup complete by **12:00**.
The new private campaign is `/shared/present/durability-handsfree-oct20/`.
It reuses the two validated seeds, qualified preflight and immutable fixture:
raw origin records, independent staging acknowledgements, fixture archive and
zero-write plan are bound by an import receipt and revalidated. A fresh ledger
imports no old operation/boot/error state or measured sample. All old nonces,
including the failed sample's, remain excluded. No seed or preflight boot needs
repeating; fresh Fedora, exact-target, fixture and tool checks precede mutations.
The source worker snapshots the current PXE default after the new release.

The versioned logged agent and unchanged sibling helpers are deployed and
hash-verified through the horse launcher; the original manifested kit is not
modified. A read-only cleanup operation verifies no active scratch helper,
consumer or holder, an exclusive read-only target claim, same-phase Fedora boot
and empty BootNext before firmware-only cleanup after a failed restore. Preserve
the failed request/inflight record separately before cleanup; never retry or
accept it. Unknown quiescence still reports incomplete restoration. Record and
clear phase-specific reboot markers before each restore so earlier sample boots
cannot qualify cleanup.

The prepared agent digest is
`987965eeb068f274558f60941b4e315a527152e407918c963a5c4a8842c51874`.
Its seven ordinary local logging checks pass; 24 fake-transport checks cover
import/refusal, nonce exclusion, dated noon bound and failed-restore cleanup.
The unchanged worker retains its prior local qualification. Native deployment
and measured results must be recorded separately; preparation is not a pass.
The eight-sample schedule, bounded thirds, early-stop thresholds and ten-minute
cleanup reserve are unchanged. Completion restores today's saved payload/menu,
PXE-first and empty BootNext, leaving Fedora running.

### Partition refresh failure and correction (2026-10-20)

The first measured-only attempt stopped at 08:02:39, still in Fedora restore.
The new persistent diagnostic showed `sgdisk --verify` succeeded, then
`blockdev --rereadpt /proc/self/fd/3` failed with BLKRRPART/EBUSY. Cleanup reached
a verified failed boundary at 08:02:42: source/menu restored, PXE-first,
empty BootNext, Fedora running, failed request preserved, zero samples accepted.

The helper retained an O_EXCL disk claim while `blockdev` reopened its proc-fd
path nonexclusively. Linux's [partition rescan](https://github.com/torvalds/linux/blob/v6.18/block/genhd.c#L342)
acquires a separate claim for that nonexclusive descriptor, conflicting with the
existing owner. A temporary loop image on this VM (Linux 6.19.10) reproduced the
same EBUSY; ioctl on the original exclusive descriptor succeeded. This explains
the confirmed failure without attributing it to transient udev activity. The
seed path never calls this Fedora restore helper: Pyxis writes the seeds, and
Fedora discovers their partitions during the subsequent boot.

The private correction keeps the exclusive claim and issues BLKRRPART directly
on its original descriptor. It settles udev first, permits at most three ioctl
attempts only for EBUSY within a 15-second refresh budget, settles afterwards and
requires the complete kernel partition-number/start/length map to equal the
verified GPT. A temporary loop image passed that exact function and its two
partition checks; devices/images were removed. Fourteen local mocked checks
cover transient/persistent busy, other errors and mismatched/extra partitions.
Data restoration, full-byte verification and measured payloads are unchanged.

The fresh campaign `/shared/present/durability-handsfree-oct20-r2/` binds the
fixed helper, patch and loop-check evidence in its input manifest. The helper
SHA-256 is `3f4612254f1cecb6f567ae44ae8c2fc100682382407e339e06742e136e99ed60`.
A new version directory binds all sibling helper hashes. It reuses accepted
seeds/fixture and quarantines both failed sample nonces; the prior failed ledger
remains unchanged. The source worker waits for the orchestrator's one-shot
launcher. Native qualification of the fixed restore and actual measured results
remain pending; the noon cleanup deadline is unchanged.

### Router-LAN resume (owner go, 2026-10-20 about 08:57)

Round2's native restore succeeded, including the corrected partition refresh.
Its first sample was unaccepted: the controller returned to Fedora but found no
matching guest result, then restored source/menu and firmware defaults at
08:13:40. The owner reports that no Pyxis ran during the intervening network
wedge. A switch locking up on link events is suspected, not established as a
Pyxis defect. The owner moved the built-in Ethernet cable to a router LAN port,
removed Fedora's leftover device override, and reported a full
Fedora/PXE/Pyxis/Fedora cycle without a wedge.

The newly authorized campaign is
`/shared/present/durability-handsfree-oct20-r3/`. Its fresh ledger imports the
accepted seeds/fixture and qualified preflight with hashes, plus round2's
successful restore/failed-boundary evidence. All earlier failed nonces remain
excluded. The fixed restore helper and all measured payloads are unchanged.
Fresh source staging snapshots today's current default after release.

The controller addresses the target by its Tailscale SSH name; it does not bind
to a LAN address, interface name or route. Before firmware changes, its private
Fedora check records default routes, reads the built-in PCI function's driver
and network interfaces, and requires the expected driver with no override,
carrier and IPv4. It changes no network or driver configuration. Deployment
records this real check separately; a prepared check is not native evidence.
Twenty-six local mocked resume checks pass, including refusal for an unready
wired interface or changed round2 provenance; the 14 refresh and seven logging
checks are unchanged. The source worker waits for the one-shot horse launcher.
Cleanup remains due by noon with the original reserve and early-stop policy.

### Review stop and proposed corrected comparison (2026-10-20)

The r3 source STOP flag was set while sample2 was active. Both samples completed
content/cleanup validation; no sample3 started. The controller restored saved
source/menu, PXE-first and empty BootNext, leaving Fedora at09:19:25. The native
first pair is USB-confounded; preserve it as exploratory/functional evidence.
The corrected baseline and exact per-binary/fs identities are in the
[measurement record](explicit-durability-measurements.md#matched-inputs).

Pending owner confirmation, propose ABBA with existing thresholds applied to one
primary envelope per pair, plus AC checks before/after and a30-second post-restore
idle. Other envelopes remain reported but cannot multiply early-stop triggers.
These are proposed changes to the accepted Oct19 policy, not retroactive
changes to the completed samples. The noon deadline and cleanup reserve remain.
