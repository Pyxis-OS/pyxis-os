# Capability forwarders: task 1 checkpoint

This is qualification in progress, not task completion. The native/exported
RETARGET choice is pending in the
[milestone](../../../wip/capability-model-implementation.md#retarget-transport-boundary-pending).

## Inputs and baseline

- Kernel base: `3b714754` (unchanged main). Userland dependency:
  `3e1e4e87c0ba4b3b7ed48a38e57d23c3575496b2`, PR #259.
- Existing compiler image: `pyxis-llvm23.1.3-e0801c2`; no compiler change.
- Ordinary `make -j16 image`; nested KVM Q35, 2 GiB, 1/4 CPUs, VirtIO-net/rng,
  standard VGA, OVMF. Host remote client connects to guest TCP 2323 through
  localhost 24084. No measurement instrumentation or new klog lines.
- Unchanged-main image build and 4-CPU boot passed. One local CALL64 baseline
  (`256` messages, `100` rounds) returned 100 verified samples: median
  123.927 ms, range 87.598–805.372 ms. The VM/host was not isolated and a build
  overlapped part of the run; this does not support performance attribution or
  replace the required native baseline. No matched after timing is claimed.
- Beta's native gate is pending in
  [#825](https://git.internal/PyxisOS/pyxis-os/pulls/825). Its record is a merge
  prerequisite; only the local functional baseline is complete.

## Manual qualification

Run each existing counter mode in its own remote session:

```text
session bin://counter.pxe --forwarders bin counter.pxe textfs.pxe
session bin://counter.pxe --queued-cut
session bin://counter.pxe --cut
session bin://counter.pxe --retire-full
session bin://counter.pxe --exit
```

An intermediate 4-CPU image passed all five: nested directory/file lineage,
COPY, explicit/last-controller CUT, supervisor-copy takeover, slot retarget,
empty-slot NOT_FOUND, authority/type refusals, unmanaged-slot persistence,
queued DENIED/NOT_DELIVERED, delivered REPLY after CUT, stale cut handles after
ID reuse, RETIRE with a full queue and provider-exit cleanup. Every remote
session returned status 0 and complete group drainage. GDB found no outstanding
operation context or temporary derived nodes after cleanup, only the twelve
live bootstrap provider roots.

Final checkpoint source adds bounded target-chain/status/restore checks.
Its rebuild and 1/4-CPU rerun are pending at this commit. Provider OPEN/PREPARE
and HTTP COMMIT result lineage, held files across service replacement and
asynchronous completion denial need direct runtime checks. No native forwarder
results, allocation-failure injection or cross-carrier FILE retarget are
claimed. Raw JSONL/GDB captures remain local under `build/`.
