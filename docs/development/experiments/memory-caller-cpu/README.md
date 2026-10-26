# Caller-CPU MEMORY qualification (2026-10-26)

Task 1 of the accepted [BSP-removal proposal](https://git.internal/PyxisOS/pyxis-os/pulls/1251).
No ABI operation, grant, worker-placement policy, per-CPU cache or live-root
mutation is added. [Execution and remaining limits](../../../kernel/memory.md#execution).
Native qualification remains open; all results here are nested QEMU.

## Provenance and baseline

The baseline was captured before editing kernel code: main `93adf240`, three
fresh boots each at 1 and 4 vCPUs, five samples per size and fixed CPU placement.
The implementation was then rebased onto main `742817b0`; kernel/arch/ABI/Makefile
sources on that main are identical to the baseline. Dependency pins advanced;
the qualification programs and SDK stayed byte-identical for comparison.
Timed B is `c67849d2`; subsequent source changes are comments/documentation only.

- Clang 23 builder `pyxis-llvm23.1.3-3d69e83`, normal `-O2`, kernel
  `-mgeneral-regs-only`, compiled log ceiling debug and console info.
- Nested KVM q35, `host,+invtsc`, 4 GiB, 1 or 4 vCPUs, OVMF/Limine,
  VirtIO SCSI CD and rng; no network, writable disk or debugger during timings.
  HWP unavailable in this VM; eager XSAVE `0x7`, 832 bytes on every CPU.
- Fixed single-CPU spaces: CPU0 on both configurations, also CPU1 on four CPUs.
  A private launcher calls the existing unchanged allocbench with MEMORY/profile,
  clock and ordinary explicit grants; it does not add a benchmark framework.
- A kernel SHA-256:
  `30b2617f75c0f903a8fbbbb67011419fe193cbad1d12fe5a0ea74de76cd7e559`.
  B: `af71460e4a23958b630cbef5a680f2e4ba0da2a1eef8961773047ca4954688c4`.
- Matched A/B/A/B uses one byte-identical RAM archive:
  `0c4f053f4ddbfc7497ae9eb18cbff2d7a56d06303769dc4ef82b3cf94eac6613`.
  The collector's checkout revision is distinct from the selected ELF's source;
  the ELF hashes above and saved baseline provenance identify A and B.

Existing allocbench `pages --profile` uses size 4096/live 16/rounds 16,
or size 1048576/live 4/rounds 4. Every allocation/release and byte check passed;
zero failures. Clock-call cost is reported by the program, not subtracted.
Each A/B pass is a fresh boot and contributes five samples per row. The table
combines each variant's two passes: median and full range, ten samples each.
These are elapsed instrumented workload times, not CPU time or PMM attribution.

| vCPUs | Caller CPU | Size | A BSP, ms | B caller CPU, ms |
| ---: | ---: | ---: | --- | --- |
| 1 | 0 | 4 KiB | 2.453 (2.399–3.736) | 2.530 (2.472–3.666) |
| 1 | 0 | 1 MiB | 3.803 (3.719–4.771) | 4.410 (3.681–4.927) |
| 4 | 0 | 4 KiB | 2.410 (2.391–3.561) | 2.486 (2.472–3.653) |
| 4 | 0 | 1 MiB | 3.719 (3.689–5.515) | 3.696 (3.658–5.486) |
| 4 | 1 | 4 KiB | 10.459 (9.771–11.124) | 2.494 (2.450–2.641) |
| 4 | 1 | 1 MiB | 5.621 (5.225–6.293) | 3.721 (3.643–4.542) |

CPU1's profile separates service from request elapsed time. For 4 KiB allocation,
median request mean falls from 23.450 to 6.117 µs; service mean is 4.784 versus
4.554 µs. Release request mean is 15.942 versus 2.939 µs, service 1.577 versus
1.454 µs. Removing the cross-CPU route accounts for most of the observed change;
these data do not isolate PMM locks, frame search, zeroing or instruction costs.

CPU0's short 4 KiB batches are about 3% slower. The short one-CPU 1 MiB median
also rises, with overlapping ranges. A separate longer-batch A/B/A/B control
uses the same existing allocbench, live 4/rounds 64, five samples per pass:
A 62.271 ms (61.660–62.840), B 61.729 ms (60.291–63.088). That control does
not reproduce a sustained per-byte regression, and does not erase the short
batch result or establish its cause. No native performance claim is made.

Task 2 begins by measuring the shared PMM after dispatcher removal and bringing
chronium the frame-allocation decision; the existing PMM lock/search and local
scratch path remain unchanged here. No cache policy is silently selected.

## Functional and ownership checks

Private C/native-call fixtures and the existing Go task-2 parallel/retirement
sources were built with the retained SDK and merged Go `f9346269`. Fixtures are
qualification artifacts, not merged boot tests. One- and four-vCPU runs cover:

- Stepped 16 MiB eager allocation, zero backing and an input message contained
  in its own released allocation.
- Aligned sparse reservation, repeated commit preserving existing bytes,
  self-contained decommit input, zero recommit, stale identity refusal.
- Failed thread-context preparation releases its provisional pin. Four native
  siblings compete for MEMORY, preserve bytes and YMM15 through the kernel-root
  switches, and retain guarded stacks until actual completion. BUSY retries
  are required while other siblings still mutate, even after one has retired.
- Address-wait withdrawal returns BAD_BUFFER; a staged pipe reader survives
  mapping changes and receives its byte after reentry. No mapping borrower is
  kept across either wait.
- Two independent processes run these checks concurrently on four CPUs.
  Each also completes the existing Go copied-stack/GC/channel/mutex workload
  (512 jobs, peak 16 overlapping job handlers) and 128 locked-M retirements.
- Invalid second batch handle refuses with no published children. A stopped
  large-allocation child is observed through actual process retirement.
  Its ready pipe synchronizes the stop test before the parent's short sleep;
  a start-relative delay alone was insufficient to establish in-call coverage.
- A 256 MiB guest requests 512 MiB sparse/eager backing normally (no injected
  fault). Both return NO_MEMORY, prior sparse bytes survive, subsequent small
  commits/allocations are zeroed and succeed, and all identities release.
- Four-vCPU `host,+invtsc,-xsave,-avx,-avx2` exercises the FXSAVE fallback;
  the C fixture reports YMM unavailable rather than executing AVX.

Read-only hardware breakpoints, outside timing runs, establish on B:
CPU1 enters `memory_request_execute` with owner CPU1, phase MUTATING, roots=0,
borrowers=0, its user task in kernel-root mode, and CR3 equal to the kernel root
(`0x62000` in that boot). A later breakpoint sees CPU2 between backing steps
of the 512 MiB request with stop_requested=true, the same exclusive phase,
zero roots/borrowers and its retained request still FORWARDED. Continuing leads
to TERMINATED and completed retirement, not a discarded call. No data-register/kernel-memory
edits or failure hooks were used.

Build, diff and released-header ABI checks pass. No new default-boot log output.
Evidence stays in `/shared/present/bsp-removal/`: ELF/config/command/program
hashes, initial baseline and matched raw logs, `matched-results.json`, longer
controls, functional runs and `ownership-stop-gdb.log`. Early private harness
failures and the superseded unsynchronized stop attempt are not counted as
qualification passes. [Native vela A/B/C](../memory-native-abc/README.md) now passes the task 1/2
functional packet; the shared PMM measurement and batches are recorded
[separately](../frame-batches/README.md). Native unrelated mixed-load stress
remains open; launch/final reclamation and shared kernel VM stay BSP-owned.
