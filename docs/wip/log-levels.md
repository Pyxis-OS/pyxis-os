# Kernel log levels

Status: **direction accepted by the owner, 2026-10-14; not implemented and not
assigned.** The task's scope note settles the remaining details.

## Problem

The kernel has two levels: `klog`, always compiled in, and `ktrace`, compiled in
only with `LOG_LEVEL=trace`. About 400 `klog` and 120 `ktrace` call sites share
them, and roughly 60 `klog` lines are errors or warnings with nothing marking
them. In a trace build, per-operation lines (one per idle CPU, per process exit,
per HD Audio verb) scroll the lines that matter off the Caelum tab: on vela, the
xHCI capability lines were present but unreadable. Deleting those lines would
lose information that is occasionally useful.

## Direction

- **Five levels:** error, warn, info, debug and trace. One-off state changes and
  inventories that `ktrace` carries today become debug. Per-operation detail
  becomes trace, the deepest level, rather than being removed.
- **Every record carries its level** in the log ring and in UDP capture, next to
  its existing subsystem prefix.
- **Two controls:** `LOG_LEVEL` sets the compile-time ceiling, so arguments of
  deeper levels are not evaluated in ordinary builds. A runtime console threshold
  decides what the Caelum tab draws. The ring keeps every compiled-in record, and
  `log` can filter by level when reading.
- **Default console threshold: info**, which keeps today's screen. A `log.level=`
  boot option changes it, with the warn-and-default rule for invalid values.
  Errors and warnings are drawn in a distinct colour.
- **Per-subsystem thresholds are deferred.** Records keep their subsystem
  prefix; separate thresholds come only if the global ones prove too coarse.

## Migration

Classify the existing call sites without rewording their messages. The parked
[#851](https://git.internal/PyxisOS/pyxis-os/pulls/851) list is the starting point
for trace: the per-idle and per-exit scheduler lines, and the HD Audio per-verb
lines, whose per-sequence summary becomes debug.
