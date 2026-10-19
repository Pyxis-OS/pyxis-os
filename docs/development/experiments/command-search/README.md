# Command-directory list qualification

2026-10-19, task 1 only. Nested Q35/KVM host CPU, four vCPUs, 2 GiB, OVMF,
standard VGA, VirtIO network/rng; task image also used a private virtio-fs export.
Existing builder `pyxis-llvm23.1.3-3d69e83`, `make -j8 image BOOT_MENU_TIMEOUT=5`.
No native/PXE action, timing claim, fault injection or merged test framework.

## Baseline and build

Baseline main `19c3cfc82e61`, userland `1836d2a`, ports `cbc017ee`, fs `d2fb97c5`:
official image artifact 13970, workflow global ID 3326 (UI run 3222), with the
embedded kernel revision checked. Remote shell echo, directory listing, Lua and
completion echo all exited zero. Source inspection confirmed scalar CAP/TEXT
storage and no command-list constructor. This was a functional baseline.

Full task image builds passed with kernel changes through `ca89802c`, userland
`4f8aece9385f821f13df602d10bf49d81fe82d9a`, ports
`e9906abbc1fd95233ebb8b4c30c2eaf9cff36ef1`, unchanged fs. Kernel build first
caught a missing native memory-copy declaration, corrected before image builds.
No compiler/container rebuild or upstream download fallback was needed.

## Observed

A private ordinary SDK client constructed/published two directory entries and
reported the independently chosen expected counts and masks in its children.
It was loaded from virtio-fs, with a small manifest explicitly declaring its
launcher/pipe resources; raw-launching that parent did not provide the pipe
needed by a nested stock shell. The corrected caller passed all cases:

- Direct child, Lua `pyxis.run`, libuv `uv_spawn` and a stock shell script each
  received both entries, in order. Every child exited zero.
- Creating a narrowed one-entry list changed the list received by the next child;
  an earlier capture retained both original entries and its directory remained
  usable after source handles closed and the environment publication was replaced.
- Empty exported list reached a child with count zero. Private and removed
  publications produced NOT_FOUND in children, with no list invented.
- Write-bearing members, capacity overflow and a read-only environment factory
  refused. Out-of-range GET returned NOT_FOUND. Capacity reported 16.
- A revocable directory member retained its wrapper: GET after CUT could return
  the cut handle, but directory use returned DENIED with no FILE grant.
- Explicit SLOT and REVOCABLE creation targeting a list returned WRONG_TYPE.

Source review found that mutable wrappers around a grant-holding list could mix
INFO/GET captures and create a member/slot ownership cycle. `ca89802c` extends
the existing grant-container target restriction; re-review found both resolved.
The final rebuilt-image run included these refusals and all preceding cases.

Read-only GDB inspection stopped at `command_list_create`: CPU 0, count 2,
IF clear (`eflags=0x82`), first member an OBJECT_DIRECTORY with masks 7.
No debugger function calls or state changes were used. QEMU shut down through
the power overlay; the private filesystem daemon exited on disconnect.

## Limits

List construction/capture/inheritance is delivered. Name lookup, default lists,
PATH adaptation, live package projections and native build chains remain later
tasks. No claim of concurrent mutation stress, allocator fault coverage, native
hardware or timing qualification. Explicit native job profiles were checked by
source review and remain explicit; no search authority is added automatically.

Raw logs, private caller source, build output and GDB transcript stay in
`/shared/present/command-search/task1/`.
