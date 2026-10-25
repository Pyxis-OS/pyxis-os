# cpuclock

`cpuclock` is an optional first-party userland package for Linux/Pyxis x86-64
load observations. It counts a long dependent integer-ADD chain and reports each
worker's effective rate against `CLOCK_MONOTONIC`.

```text
cpuclock --threads 1 --samples 5 --milliseconds 1000
cpuclock --threads 16 --samples 5 --milliseconds 1000
```

Pass the CPUs offered to the process as N for a one-worker-per-CPU load. The
program does not discover or change the process's allowance. Pyxis threads and
clock handles expose no CPU-count query, and global online CPU count is not
process affinity. No CPUID topology guess, per-thread pinning, MSR access or
power policy is added. Worker indices do not identify physical/logical CPUs;
siblings inherit the space's allowed set and may migrate.

The default is one worker, five samples, each requesting 1000 ms. Workload
limits are 64 workers, 1000 samples and 60000 ms. These bound the utility's
storage/output; 64 workers include main and fit the current C11 record limit.
Thread creation can still refuse earlier and reports failure, not a partial
successful measurement. Workers persist across all samples and output waits
until measurement finishes, avoiding console work during timed rounds.

Each cohort starts from a blocking generation barrier and a shared monotonic
deadline. Rows report actual elapsed time, start skew, counted adds and checksum.
A worker first scheduled after its deadline is invalid. Counted batches contain
256 dependent register ADD instructions repeated 4096 times; early-clobber
constraints keep the step in another register. The checksum verifies arithmetic
consistency modulo 2^64. Optimized disassembly establishes the machine-code
sequence; neither is proof of a hardware clock rate.

`GHz = adds / elapsed_nanoseconds` assumes **one dependent ADD per core cycle**.
It is a wall-time estimate: loop/clock overhead, descheduling, interrupts, SMT
contention and migration can lower it. Validate the assumption on horse against
an independent **under-load** hardware-frequency observation before treating
numbers as calibrated clocks. A CPUID advertised maximum is not that observation.
Hardware/power diagnosis and native windows belong to the separately assigned
P0 investigation. No frequency result from a nested VM is a native baseline.

The bundle declares ordinary memory MANAGE, threads MANAGE and clock
READ|PRECISE. Untimed conditions use native thread parking; no clock sleep,
network, system_info, random, launcher or hardware authority is requested.

## Build and collect

With the existing compiler container and an exported SDK:

```sh
make -C userspace SDK=/absolute/sdk BUILD=/absolute/native-build cpuclock
```

The regular userland install stages `cpuclock.pxb`; it is packaged by
`packages/programs/cpuclock.lua`. It stays outside the core archive's rescue
selection. Package against the real release build identity for an ABI-0.0.8
LAN tree; do not rewrite ABI fields to bypass admission.

The same C source builds on Linux with its ordinary host compiler/C11 threads:

```sh
make -C userspace/cpuclock BUILD=/absolute/linux-build
/absolute/linux-build/cpuclock --threads 1 --samples 5 --milliseconds 1000
```

Save full output per run. The last line is PASS/FAIL plus min/mean/max across
all worker samples; rows retain the per-worker trend needed for turbo/thermal
observations. Invalid samples, clock/synchronization errors and output failures
return nonzero. If thread retirement is unconfirmed, immediate process exit
keeps shared context alive until the kernel stops every sibling.

## Qualification (2026-10-25)

The Linux and native optimized objects each contain 256 dependent register
ADDs inside the 4096-iteration batch loop. Linux C11 and Pyxis C23 builds passed
with warnings treated as errors. On four-vCPU KVM (`q35`, `max,+invtsc`, 4 GiB),
the native bundle passed two one-worker samples and three four-worker samples
at 20 ms; all 14 rows had valid timing and matching checksums. Zero workers and
duplicate `--threads` returned status 1. A separate idle GDB inspection found
all four CPUs back in their scheduler idle path. The unchanged guest kernel
was `e57d9701e051`; these are nested-VM correctness checks, not frequency
calibration. The Linux build also passed one/four-worker smoke runs.

The LAN package is stamped using the real tagged 0.0.8 release identity and
passes the package checker for ABI 0.0.8. Native calibration on horse and
vela/Fedora/Pyxis comparison remain scheduled through the orchestrator.
