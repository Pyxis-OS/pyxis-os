# Shared ESP payload stager qualification

2026-10-15, disposable QEMU images only. The
[helper contract](../../../../userspace/installer/ESP-PAYLOADS.md) belongs to
userland #272; Pyxis #880 pins it. Installed activation and consumer publication
remain separate work in #797 / userland #253 and NVMe coexistence task 3.

## Build and runtime inputs

Helper source `f14dc1325d35` is byte-identical to the C source exercised here.
Userland main is `1fce25df8233`. Existing LLVM builder `df87791` builds the
focused installer and full ordinary image. SDK/ports exports from #867's API
run 2653 match the unchanged ABI and pinned fs/ports inputs. Rebase onto main
`ffee27f4` includes merged #868; its ordinary image build also passes.

The existing #797 scratch frontend was rebased in a **never-merged local caller**
`01c22924cd39`, using the shared stage/verify/close adapter. Its optional fourth
payload is 113 bytes of reviewed input text, not a qualification of a consumer's
printed-entry generator. The shipped installer does not call this helper yet.
No new test framework or native writes were introduced.

QEMU 10.2.2, nested KVM, Q35, host CPU, 4 vCPUs, 2 GiB, OVMF, standard VGA and
modern virtio-blk. The runtime kernel has main `b030c7b1`'s kernel source,
SHA-256 `bfe59107ae05e537a7858da7c991897c1b63f43858554350b9ce790c1b2481ed`.
It predates the #868 rebase; this is not exact-current-parent runtime evidence.
Exact submitted-head CI and the later image build are separate checks.
The full live archive carries the local caller and trusted archive catalog;
no stale payload substitution or native firmware is involved.

## Observed results

- The ordinary image boots all configured live spaces; guest powered off.
- An 80 MiB GPT disk / 40 MiB ESP refuses the reviewed 46.26 MB requirement.
  Cancel leaves the complete image byte-identical.
- A 256 MiB GPT disk / 128 MiB FAT32 ESP with 512-byte clusters has ample free
  space, but the original full-file resize hits CALL_LIMIT on the 5,837,840-byte
  kernel. The fresh zero-length file is retained. FAT32's one-operation plan
  exhausts its bounded work budget; no limits were widened. The helper now
  grows fresh files through bounded sequential writes.
- That corrected geometry copies/syncs the complete kernel, independently
  matching its source SHA-256. The run was deliberately interrupted during
  archive copying; it is not a completed-attempt pass. Foreign file/menu bytes
  and GPT remain unchanged/valid.
- A fresh 768 MiB GPT disk / 512 MiB mirrored-FAT32 ESP with 4 KiB clusters
  completes all payload copies, sync, reopened identity/extent/hash verification
  and the subsequent explicit `verify()` call. The retained result reports
  `synchronized=true`, `staged_verified=true` and CALL_OK.

A temporary hardware breakpoint stopped the caller at
`config_storage_publish_boot`, after both helper APIs succeeded and **before
configuration publication began**. GDB only inspected state; calls and target
memory/register writes were disabled. The record still reports no committed
configuration or boot activation. QEMU was closed at that boundary, then the
files were inspected offline. All owned QEMU/debugger jobs are stopped.

Attempt: `EFI/pyxis/b030c7b1662d/fc16bb2a0558c958fc3ce7622c00f5a5/`.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| kernel.elf | 5837840 | bfe59107ae05e537a7858da7c991897c1b63f43858554350b9ce790c1b2481ed |
| boot.arc | 40375612 | 150f50b6334d368274e531fd08373aa970d7216d5e3badc8c625f59e3b179205 |
| revision.txt | 13 | a7803a137ade64307f11adeb6a9e84036b5a4359e99b1edd512d73505ef819b8 |
| entry.txt | 113 | 4dba03c5b4004c9cc2f0dd28d87c5c26211e7fd543ef77917eeaf3b5d7f9589e |
| ownership.record | 637 | 7eb578f03affa73695b634f9af3ae29f4d060cb6fdf6b4b23c747fb403668b09 |

Offline sizes/hashes match the retained record. The kernel also matches the
supplied ELF byte-for-byte; all 933 regular archive files (40,228,418 payload
bytes) match their supplied archive contents. The foreign EFI file and Limine
configuration compare byte-for-byte unchanged. GPT verification passes.
Read-only `fsck.fat -n` returns 1 for the **dirty bit only**, with no reported
structural problem; it leaves the image unchanged. This is expected because
the caller's retirement/unmount step was not run. Sync is not unmount, and
this is not a power-loss or successful-retirement qualification.

## Limits

This qualifies payload staging and retained verification, not configuration
publication, entry switching, previous-pair cleanup or the 30-second publication
budget. Those require the actual consumer checks. Nested small-call copying
and verification were slow; no native performance conclusion is drawn, and
neither adequate free space nor the 1 GiB input limit promises a completion time.
No deadline was extended, mutation retried, failed attempt removed or filesystem
repaired. Host images, debugger/serial logs and captures remain local under
`build/qualification/`, with a task-specific handoff.
