# Interrupt clock cache (2026-10-21)

Task 1 of [clock reads](../../interfaces/clocks.md). Ordinary builds before and
with code `7f6b3628` pass using the existing `pyxis-llvm23.1.3-3d69e83` image.
No public ABI, SDK, compiler-container or package extraction change.

## Matched installed workload

Use the [baseline](clock-read-baseline.md) command, archive and frozen installed
0.0.7a image: q35/KVM, no invtsc, four CPUs, 4 GiB, VirtIO block/network/RNG,
OVMF, VM-local raw disk and cache=writeback. Three serial fresh clones select
HPET direct 64-bit. Kernel/ABI/fs inputs are unchanged between the baseline
`81313e0e` and task base `111cadba`; only the reviewed kernel changes in this
comparison. Installed program bytes and the pool boot configuration stay exact.

The installed selector requires the running kernel revision: rename its existing
binary revision directory to `7f6b362805fd` and rebind the existing configuration
record and ESP ownership/revision records through the configuration codec. This
is preparation of a disposable fixture, not a new userland build. All boots
confirm committed selection, the new binary selector and the original pool
configuration, without fallback. Changed kernel ELF SHA-256:
`daddd4fcb402031fbdd7a6e8bd9b933a039472237de1375e5e352e509e36eb98`.

| Nested sample | Verify display (s) | Extract zero → first positive (s) | Extract zero → total (s) |
| --- | ---: | ---: | ---: |
| After 1 | 195.02 | 55.46 | 281.32 |
| After 2 | 184.01 | 50.39 | 245.84 |
| After 3 | 179.63 | 51.15 | 257.69 |

Verification median **184.01 s**, range 179.63–195.02 s, versus baseline
435.39 s (432.82–436.08): 58% lower. Displayed extraction median **257.69 s**,
range 245.84–281.32 s, versus 624.00 s (571.77–665.09): 59% lower.
Zero-to-first-positive median 51.15 s (50.39–55.46), versus 138.71 s
(118.02–141.36). All commands exit 0 with complete Remote drain.

Three separate **idle** 10-second windows per image, after the same 30-second
startup settle and without a Remote command, count HPET MMIO through the same
four-vCPU perf filter used in the baseline. Before: 3,247 / 3,253 / 3,302 reads/s;
after: 2,157 / 2,160 / 2,174 reads/s, median **34% lower**. These are three
windows in one boot per state, not three independent boots.

A separate discarded clone's **fetch-only** 10.157 s window counts 1,016,092
HPET reads: **100,041 reads/s**, about 33,347 high/low/high clock samples/s,
versus baseline 94,388 reads/s. This busy rate does not show a reduction and
is not normalized by completed work; it cannot establish reads per installation
or the share of extraction spent in clock reads. No perf or debugger runs
alongside the accepted after verify/extract samples.

The first positive display remains 2/8778, after the 90,417,950-byte Clang
executable; it includes real file writes. Display envelopes are host receipt
times, about 200 ms quantization, including Remote delivery. Extract-to-total ends before final sync/publication. They are
not interchangeable with the native guest extraction+sync envelope. Shared VM
scheduling/storage caches introduce variation; these are nested measurements,
not an exclusive-host experiment. The matched native result follows.

## Native ThinkPad T14 (2026-10-21)

Three fresh boots before (`c35788ec`) and after task 1 (`ff456687`) use the same
32 GB SanDisk directly attached, built-in Ethernet, 12 CPUs and AC confirmed
before/after every sample. Both select qualified native TSC at about 2096 MHz.
The initrd is byte-identical (SHA-256 `f236b206cbebbc74…`), including the private
guest extraction/sync brackets. The same 373,117,440-byte Clang archive
`3e1a92d1a5742515…` contains 8,778 files; all six installs exit 0 with complete
Remote drain. See the [baseline](clock-read-baseline.md) for commands and fixture.

| Native sample | Verify display (s) | Extract zero → first positive (s) | Extract + sync, guest (s) |
| --- | ---: | ---: | ---: |
| Before 1 | 14.620 | 12.269 | 75.706 |
| Before 2 | 7.722 | 11.135 | 72.487 |
| Before 3 | 7.813 | 10.428 | 69.026 |
| After 1 | 7.595 | 10.617 | 74.321 |
| After 2 | 7.607 | 12.687 | 71.205 |
| After 3 | 7.600 | 10.854 | 77.520 |

Medians (ranges): verification **7.813 (7.722–14.620) → 7.600
(7.595–7.607) s**, −2.72%; first positive **11.135 (10.428–12.269) →
10.854 (10.617–12.687) s**, −2.52%; extraction through final sync
**72.487 (69.026–75.706) → 74.321 (71.205–77.520) s**, +2.53%.
Extraction ranges overlap substantially; these samples establish no attributable
native improvement or regression. The large nested HPET reduction does not
carry over to this native TSC configuration.

Verification/first-positive are host receipt envelopes, with the display and
transport limits above. First positive is 2/8778 after the large Clang executable,
not the first file alone. Extraction/sync uses guest precise-clock brackets.
The three before boots precede all after boots, rather than interleaving; the
first before sample is cold (fetch 11.27 s versus about 5.8–6.0 s thereafter).
Cache and media variation limit attribution, especially verification.

An initial after attempt stopped at repository admission with no measured
package work: the repository had been republished in a newer index format.
The accepted r2 uses `/0.0.7/public/`, with the exact baseline archive verified
again; that verification warmed the host cache. No failed attempt is a sample.
Raw captures and derived `results/comparison.json` remain under
`/shared/present/hpet-hot-reads/thinkpad-clock-after-task1-r2/`; before captures
are in the sibling `thinkpad-clock-baseline/results/BEFORE1..3/`.
PXE restoration was verified byte-identical after the campaign.

## Functional and debugger checks

Separate 1- and 4-CPU installed clones pass short/past sleeps, contended 32/64-bit
parking, mismatch/refusal, timeout, unmap, process stop, the TLS/errno/runtime
probe, and verified CALL/SEND at 64/4096 bytes (256 messages, five rounds).
IPC runs are functional checks, not matched IPC performance measurements.

With QEMU's GDB, the timer publishes its supplied precise sample to the cache
before expiry. A periodic npfs worker parks as a kernel task using cached time.
A future address WAIT is cancelled with TIMED_OUT only after its deadline;
bucket detachment occurs on the scheduler stack before notification. Unmap
leaves no bucket registration on either CPU configuration, and all deferred
expiry lists are empty. Four CPUs return to interrupt wait. Disassembly of the
coarse reader contains no hardware read. No injected debugger calls are used.

Two validation limits are retained in the raw record. The existing aggregate
`threadprobe runtime-time` fails on both the unchanged control and new kernel:
it expects detached attributes/self-detach to return ENOTSUP, while this runtime
accepts detached attributes and returns EINVAL for detaching the initial thread.
It does not separately report its timing subchecks, so the aggregate is not a
passing timing test. The first SMP software-breakpoint session hit a GDB
internal thread assertion; its clone was discarded. Subsequent single-CPU
hardware breakpoints and four-CPU read-only inspection completed.

Raw build, debugger and functional captures are under
`/shared/present/hpet-hot-reads/task1/`, alongside `qemu-task1-1..3`,
`comparison.json`, `hpet-fetch-perf.*` and `hpet-idle-{before,after}.json`.
Installed preparation and measurement scripts stay there. Local frozen disk
fixtures were removed after milestone completion; raw measurements remain.
No production counters, new tick or klog lines were added.
