# Installed executable VM-owner admission — 2026-10-29

Issue [#1577](https://git.internal/PyxisOS/pyxis-os/issues/1577) reported base-set
commands returning `Resource busy`, particularly immediately after `spaces`
exited. This is shared-VM contention, not an executable read lease retained by
the exited process.

## Observed cause

On kernel `31e07a36d096674423ef0b48c46fa0ae062346be`, a four-vCPU nested KVM
run reproduced four failed npfs-backed launches out of 60 commands. A second
run reproduced three. GDB stopped at the failed `vm_kernel_owner_try()` branch
in `launcher_start()`. The second capture also inspected the current owner's actor and found
`kernel_entry = reaper`; the request stack was
`request_worker -> launcher_request_execute -> launcher_start`.

The reaper legitimately retains the structural owner across yielding shared-VM
reclamation. The launch executor previously tried once with interrupts disabled
and exposed that ordinary contention as `CALL_BUSY`. npfs image snapshots also
had an independent try-only allocation. Immutable boot-archive executables skip
the selected-image copy and its subsequent retirement, explaining the extra
contention from installed commands. There is no package-set lock in this path:
launch preparation releases the selected file reference and retires the copy
before the child runs.

## Correction

Launch preparation (single, space and batch) and RAM-image capture use the
existing one-second shared-owner admission wait before entering the
interrupts-off mutation. The servicing request retains its inputs while it
waits. Deadline failure takes the existing file-operation, capture and reserved
slot cleanup paths. npfs, FAT32 and host image snapshots use the same bounded
admission before allocation and release ownership before storage reads.
No launch is retried, and no ownership or durability check is removed.

## Checks

The same disposable 256 MiB npfs volume, exposed from a GPT VirtIO disk, held
`cat`, `jq`, `less` and `fastfetch`; the boot archive supplied a `jq` control.
The volume was opened read-only. Every group ran those four npfs commands,
then the archive command, twelve times. `less --version` intentionally reaches
its existing unsupported-option diagnostic (exit 1); it checks successful
launch, not pager interaction. The other commands exited zero.

| Kernel | CPUs | Commands | BUSY launch failures |
| --- | ---: | ---: | ---: |
| Before, GDB observation | 4 | 60 | 4 |
| Before, second GDB observation | 4 | 60 | 3 |
| Fixed | 4 | 60 | 0 |
| Fixed | 1 | 60 | 0 |
| Fixed, RAM and pipeline extension | 4 | 77 | 0 |

The extension copied `jq` into a RAM file and alternated eight launches of it
with eight two-stage npfs `cat | cat` pipelines. All succeeded. Full default
kernel and development flip-enabled kernel builds passed. No diagnostic code
was added to the kernel.

These are correctness checks under nested KVM with invariant TSC exposed,
not timing results. The matched archive used the existing qualification
userland `8cefca4017769e5e79d455cfff127947554a6fc3`; its exercised file/launch ABI
is unchanged at the kernel baseline (the intervening ABI edit is the log
interface, which these commands do not use). Exact-current image construction
is checked separately by CI. Source review covers FAT32 and host admission;
those transports and chronium's installed package set were not rerun here.

Small evidence is in `/shared/present/installed-command-busy-20261029/`:
`summary.json`, GDB owner captures, command results and serial logs. No native
run or package-set mutation was performed.

## User-visible limit

Ordinary reclamation contention now waits within the existing admission budget.
A genuinely exhausted one-second budget can still return `Resource busy`;
this change does not promise an unbounded wait or retry a failed launch.
