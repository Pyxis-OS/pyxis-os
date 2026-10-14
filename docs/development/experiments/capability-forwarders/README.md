# Capability forwarders: task 1 checkpoint

This is qualification in progress, not task completion. The owner accepted
same-representation RETARGET and no FILE bridge; see the
[milestone](../../../wip/capability-model-implementation.md#retarget-representation-accepted-owner-decision-2026-10-14).

## Inputs and baseline

- Baseline: unchanged main `3b714754`. Code checkpoint: `71129776`, rebased
  onto main `e0644943` including bounded private allocation. Userland dependency:
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
- The owner's native baseline is complete and recorded by beta in
  [#825](https://git.internal/PyxisOS/pyxis-os/pulls/825); merging that record
  satisfies the task-1 gate. Repeat its profile after milestones A and C;
  this task's local functional checks do not claim a native performance result.

## Manual qualification

Run each existing counter mode in its own remote session:

```text
session bin://counter.pxe --forwarders bin counter.pxe textfs.pxe
session bin://counter.pxe --queued-cut
session bin://counter.pxe --cut
session bin://counter.pxe --retire-full
session bin://counter.pxe --exit
```

The code checkpoint passed all five on both 1 and 4 CPUs: nested directory/file lineage,
COPY, explicit/last-controller CUT, supervisor-copy takeover, slot retarget,
empty-slot NOT_FOUND, authority/type refusals, unmanaged-slot persistence,
queued DENIED/NOT_DELIVERED, delivered REPLY after CUT, stale cut handles after
ID reuse, RETIRE with a full queue and provider-exit cleanup. Every remote
session returned status 0 and complete group drainage. Interactive GDB found
no outstanding operation context or temporary derived nodes after cleanup,
only the twelve live bootstrap provider roots. Global target-depth checks,
bounded status traversal and restoration keep caretaker chains within the
tunable 32-node budget, including after retargeting an existing tail slot.

On the preceding code checkpoint `c3f650e0`, `cat text://guide` passed. A Lua
program held that file while awaiting input; GDB found its derived grant with
both provider-service and file-export ancestors. Reading after input returned
the expected first byte; closing drained the derived nodes. A service-replace
attempt in the packaged Remote space returned management DENIED, so it did
not qualify held files across replacement; that needs a management-enabled
session. Provider PREPARE/HTTP COMMIT lineage, cut-during-OPEN and asynchronous
completion denial still need direct runtime checks. No native forwarder
results, allocation-failure injection or cross-carrier FILE retarget are
claimed. Raw JSONL/GDB captures remain local under `build/`.
