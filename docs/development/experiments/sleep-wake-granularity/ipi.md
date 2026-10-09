# Deadline expiry reschedule IPI

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

This is the separately measured first implementation step from
[proposal #532](https://git.internal/PyxisOS/pyxis-os/pulls/532), whose three
defaults the owner accepted on 2026-10-08. It fixes AP notification without changing
deadline ownership, timer programming, ABI or ordinary resource wakeups. The one-shot
step follows separately.

## Publication

BSP expiry removes each due timed record and publishes its wake under the existing
queue lock. For a parked waiter it records only the destination CPU index; after
unlocking it sends one existing reschedule IPI per affected remote CPU. No task or wait
pointer survives publication/unlock. A not-yet-parked waiter receives notification
only, preserving the saved-stack handshake. No self-IPI is sent. The temporary
destination array uses the existing boot CPU count and stack storage, adding no timer
quota or heap allocation.

## Matched measurements

The source base was `92ae8783b520a007826633fe418b9bf51e97e5cf`, whose kernel source
matches the original `9acf597` baseline, plus the 12-line change recorded in `a82fca6`.
The measured ELF was built before that change was committed and embeds `92ae8783b520`,
so its revision string does not identify the later documentation head. Builder,
Kconfig, guest devices, firmware, four CPUs/8 GiB, VGA 1280x800 and nested KVM match the
[original baseline](README.md); only `boot/caelum.elf` changed in the copied image tree.

Five individually entered, unprofiled `sleep-baseline > sN` runs in Development used the
same SDL workload (ten warm-up frames, 300 measured frames) and each reported zero
sleeps below 16 ms. Median per-run mean frame time was **24.243215 ms** (range
**24.067987–24.325201 ms**) against the baseline's 25.565202 ms; median mean sleep
23.308009 ms; the largest observed sleep was 24.276990 ms against the baseline's
32.356390 ms. Render/host variation remains and this is no maximum-latency guarantee.
The IPI alone does not remove BSP tick quantization.

The same packaged `quake +map e1m1` then ran with the original capped-rate method
(`Host_Frame` boundary samples, hardware breakpoint disabled between them, no sampling
loop or inferior call). Five windows (896–1197 frames) gave a median **59.594522 FPS**,
range **59.584915–59.605471 FPS**, against the baseline's 49.222714 FPS. These
debugger-assisted windows include scheduling, display, host and profiling effects and
are neither native performance nor isolated CPU cost. Ordinary kernel build and manual
boot passed; queue/parking ownership was source-reviewed.
