# Threads task 1: process lifetime qualification

Matched before/after record for [task 1](../../../wip/threads.md), recorded on 2026-10-09.
These are nested-KVM observations, not owner-host or physical-hardware results.
Raw JSONL, timing, serial and debugger captures stay in ignored
`build/task1-baseline` and `build/task1-after`; no in-tree test, probe or benchmark
infrastructure was added.

## Inputs and configuration

The baseline uses the clean kernel, SDK, userland and ports bundles from exact-head
CI #1506, with the existing `pyxis-llvm23.1.3-49e2c1a` builder:

| Input | Revision |
| --- | --- |
| Pyxis kernel and SDK | `c1e5f3be2ef32253cf06c39942a820d6c1c6e5ed` |
| userspace | `72f303d67355dc10e4b101be2e4671929a613d6f` |
| ports | `8b918c466da8903542dea5a5e2723908385006fb` |
| fs | `b427df29f865bc361b8da92bcd74e114581e9a32` |
| lwIP | `a1aadb91a50360ff5b52864f7cec810b8162ee85` |

The after kernel uses code revision `4c560f53b220b7498e2bffd435a13bd24b590689`.
An ordinary `make -j16 image PREBUILT="sdk userspace ports"` built the kernel and
image using the verified baseline bundles. Kernel code remained unchanged during
measurement; the recorded modified checkout contains documentation edits. No
dependency pin, public ABI or compiler-container input changed. Effective
`kernel.config` files compare equal, and the two staged initrd trees have equal
file contents. The cpio archives differ in timestamps, so their bytes are not
identical.

Installed QEMU 10.2.2 runs Q35, nested KVM, `-cpu max`, 512 MiB, one socket
with either one or four cores and one thread per core, UTC RTC, standard VGA,
VirtIO RNG and modern VirtIO networking through the user backend. The host
loopback TCP port 24567 forwards to guest `10.0.2.15:2323`. A VirtIO SCSI CD
boots the ISO to avoid the installed QEMU's AHCI boot issue. OVMF uses the matching
raw `/usr/share/OVMF/OVMF_CODE.fd` and `OVMF_VARS.fd` pair with fresh writable
variables for each boot.

Only the staged initrd's Remote configuration adds `launch = true`, allowing
Lua's group-bound launcher. Tracked configuration and dependency pins remain
unchanged. Logging is `info`; memory profiling is off. The GDB socket
remains available, but the debugger is detached during timed workloads.

The four-CPU launch command is:

```sh
qemu-system-x86_64 -machine q35 -accel kvm -cpu max \
  -smp 4,sockets=1,cores=4,threads=1 -m 512M -rtc base=utc \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=build/task1-baseline/vars-4.fd \
  -device virtio-scsi-pci,id=scsi0,disable-legacy=on \
  -drive if=none,id=cd0,media=cdrom,readonly=on,file=build/task1-baseline/pyxis.iso \
  -device scsi-cd,drive=cd0,bus=scsi0.0,bootindex=1 \
  -object rng-random,id=rng0,filename=/dev/urandom \
  -device virtio-rng-pci,rng=rng0,disable-legacy=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:24567-10.0.2.15:2323 \
  -device virtio-net-pci,netdev=net0,disable-legacy=on \
  -display none -serial file:build/task1-baseline/serial-4.log \
  -monitor stdio -gdb tcp:127.0.0.1:1257 -no-reboot
```

For one CPU, use `-smp 1,sockets=1,cores=1,threads=1`, `vars-1.fd` and
`serial-1.log`. After runs substitute `build/task1-after` for the baseline paths.
Prepare each variables file by copying the raw OVMF variables.

## Workloads and results

A 16-child Lua launch batch warms the existing paths before the measured batches.
Each measured batch launches and waits for 1024 fresh `echo -n` processes, then
exits the remote root shell. The existing host client receives typed completion
and final execution-group drain. Run the following three times per CPU count,
using a distinct capture name:

```sh
printf '%s\n' \
  'lua -e '\''for i=1,1024 do assert(pyxis.run{"echo","-n"}==0) end; print("launches=1024")'\''' \
  'exit' | /usr/bin/time -f '%e' -o build/task1-baseline/launch-4-1.seconds \
  build/tools/pyxis-remote --machine --no-shell-echo \
  --columns 80 --rows 24 127.0.0.1 24567 \
  >build/task1-baseline/launch-4-1.jsonl
```

The allocation rows run these existing guest commands three times each in fresh
processes, after detaching the debugger:

```text
allocbench growth --size 65536 --live 128
allocbench pages --size 65536 --live 16 --rounds 64
```

Every batch reported `launches=1024`, normal status 0 and final `drain=complete`.
Growth reported 128 allocation attempts and releases; pages reported 1024 each.
All allocation samples had zero failures, status 0 and final group drain.
Values below are medians with minimum–maximum ranges over three samples:

| Workload | 1 CPU before | 1 CPU after | 4 CPUs before | 4 CPUs after |
| --- | ---: | ---: | ---: | ---: |
| 1024 launches, whole remote session (s) | 3.03 (3.03–3.05) | 2.98 (2.97–2.99) | 3.03 (3.02–3.03) | 3.01 (2.97–3.06) |
| `allocbench growth` (ms) | 10.631 (8.618–10.866) | 9.465 (8.655–10.713) | 8.990 (8.239–9.804) | 8.231 (8.207–9.492) |
| `allocbench pages` (ms) | 85.811 (84.814–86.830) | 84.235 (83.352–84.593) | 78.771 (77.604–79.414) | 76.767 (76.519–76.904) |

Host `time` prints hundredths of a second. Its interval includes client/session
startup, Lua startup and interpretation, transport, shell exit and final group
drain; it is not isolated syscall or BSP teardown latency. Allocation intervals
follow [allocbench's contract](../../allocation-profiling.md): memory operations
and loop work are timed, final process teardown is outside the interval. Clock
calibration is not subtracted, and nanosecond clock units do not imply that
precision. Medians changed by small amounts, with overlapping growth ranges and
variation in the complete remote-session times. These samples do not isolate a
process-lifetime speedup or establish statistical confidence, host performance or
bare-metal performance.

Offline GDB `sizeof` inspection of the matching kernel ELFs gives:

| Metadata / allocation (bytes) | Before | After |
| --- | ---: | ---: |
| Task | 784 | 752 |
| Process | 80 | 136 |
| User request storage | 4928 | 4928 |
| User profiling storage | 720 | 720 |
| Total per user task/process | 6512 | 6536 |

The total increases by 24 bytes, excluding heap overhead and the unchanged
16 KiB task kernel stack. Moving lifetime state adds no allocation.

## Debugger observations

Baseline read-only one-CPU GDB inspection observed normal retirement in `reap_completed`:
`process_destroy` ran with kernel CR3 and BSP CPU 0, after clearing the process's
control/group links. `process_control_complete` then received `PROCESS_EXITED`,
status 0, with its task link already null. No debugger function call was injected.

The real failed pipeline `echo -n | boot://share/hello.txt` reached
`launcher_batch_abort` with exactly one prepared user task. That task was neither
exited nor faulted, and the group still contained only its already published root
shell. The invalid second image therefore rolled back an unpublished first stage.
After completion, heap and PMM snapshots matched their prior values: 5770 live
heap allocations, 1716192 live block bytes, 13 pools, zero retired arena bytes,
and 9999 allocated frames. Completed-task and retired-object queues were empty
at both snapshots.

After inspection observed normal retirement on both CPU configurations. The BSP
entered `process_task_detach()` in the kernel root, detached the sole task stop
target, and freed task storage before `process_task_reclaimed()`. At entry to the
latter, the task pointer was null while `task_storage` was still true. Completion
then ran with the control's process link null, after process destruction, and
reported `PROCESS_EXITED`, status 0. The failed pipeline again rolled back exactly
one unpublished first stage: `PROCESS_PREPARING`, task storage present, result
unset, and only the root shell counted as a published group member.

A temporary guest C program, compiled with the existing TCC, stored through a
null pointer. It reached `user_fault()` on CPU 0 in the one-CPU boot and CPU 2 in
the four-CPU boot, with IF=0 and no process lifetime lock held. Final process
completion reported `PROCESS_FAULTED`, status 0. Its source and executable were
removed with `rm`; no probe remains in the tree. Source inspection confirms the
sole task publishes the terminal result through atomic `result_set` without
taking a process lock at fault entry.

In the one-CPU boot, an initial Ctrl+C delivered to the host machine client closed
that client and exercised actual group stopping through `stop_locked()`. Proper
guest Ctrl+C, subsequently delivered through the documented FIFO client mode,
stopped an endless Lua foreground command through process-control TERMINATE and
reported `PROCESS_TERMINATED`, status 0. Four-CPU Ctrl+C stopped `head -n 1` while
its input wait was interruptible, with the same result. Exiting the root shell
with an endless background Lua command exercised four-CPU group stopping. After
the last process and task were reclaimed, the group member count fell from one
to zero while `cleanup_pending` remained two and completion stayed false. Final
client `drain=complete` followed the deferred cleanup callbacks.

After the one-CPU lifecycle checks, heap/PMM snapshots exactly matched that boot's
initial values: 5770 live allocations, 1716408 live block bytes and 9935 allocated
frames. The four-CPU final snapshot held 5852 live allocations, 1719960 live block
bytes and 9959 allocated frames; its initial snapshot still included the remote
root shell, so those snapshots are not equal-state comparisons. Completed-task
and retired-object queues were null in both final snapshots. Allocation exhaustion
and concurrent shared activity remain unexercised; this slice still has one user
task per process. All task-owned QEMU, debugger and client jobs were stopped.
