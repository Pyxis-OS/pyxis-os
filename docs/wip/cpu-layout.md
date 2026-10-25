# CPU layout-aware placement

Accepted direction (chronium, 2026-10-25), **later than the HWP change and
possibly after 0.0.9**. No implementation is assigned by this document.
The current [scheduler](../kernel/smp.md) knows allowed CPU sets, load and SMT
siblings. The [native CPU record](../development/experiments/cpu-clocks.md) puts
vela's slow single-worker runs on a P-core already, so this is not a claimed
explanation for that measured gap. Qualify [HWP alone](../kernel/cpu-power.md)
before a separate placement comparison.

## Metadata needed

Architecture code must retain truthful per-CPU core type, SMT/core/package
identity, L2/L3 sharing and relative capability rank. Intel inputs are CPUID
`0x1A`, deterministic cache leaf `0x4`, topology leaf `0x1F` where available,
and each CPU's HWP highest-performance capability after HWP is enabled. Generic
scheduling consumes internal metadata rather than Intel registers or fixed CPU
indices. CPUID `0x1A` distinguishes Core and Atom types, not a separate LP-E
type; cache/cluster facts must not be replaced by a machine-specific guess.
Unknown capacity retains existing placement, including uniform AMD/SMT handling.

## Initial policy direction

Keep allowed sets and least-loaded/idle-physical-core selection primary. Among
equally available eligible cores, prefer higher supported capacity; retain
existing locality/preferred-CPU behavior at equal capacity. Use the same
preference where existing enqueue/migration rules allow it, without moving
in-kernel continuations or adding remote migration. Wide runnable workloads
still use every allowed core, including LP-E.

This favors a free P-core for an isolated task and avoids a lower-capacity core
when a higher one is equally available. It changes placement ties and can trade
cache locality or energy for capacity. It does not infer foreground/background
intent, reserve P-cores or exclude LP-E. Automatic E-for-background placement
needs an explicit work-class contract later. Dynamic HFI/Thread Director and
task-dependent ranking are later mechanisms, with no placeholder API now.

Compare this policy against the same HWP-enabled build: actual CPU/APIC
placement, one/few/all-worker loads, restricted spaces, cache locality and fair
progress. Preserve the existing CPU allowance and SMT contracts. No ABI change
or affinity API extension is needed for the accepted initial direction.
