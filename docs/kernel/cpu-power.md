# CPU performance policy

Caelum initializes Intel hardware-managed P-states (HWP) locally on each
supported logical CPU before scheduling. This is a boot policy, not a frequency
governor or a public control interface. Scheduling and CPU affinity are unchanged.
The [native qualification record](../development/experiments/cpu-clocks.md)
separates the original deficiency from the matched HWP-only A/B result.

## Local HWP request

Accepted (chronium, 2026-10-25), targeting 0.0.9 without an ABI change:

- Require Intel vendor identification, MSR support and CPUID 6 HWP support.
  AMD and unsupported Intel CPUs retain their existing power behavior.
- With kernel GS and IDT installed and interrupts disabled, establish and
  confirm `IA32_PM_ENABLE[0]` on the executing CPU. Only then read that CPU's
  `IA32_HWP_CAPABILITIES` and optional `IA32_HWP_CTL`.
- Set minimum to its Lowest_Performance, maximum to its Highest_Performance,
  desired to zero (autonomous), EPP to 128 when supported, and activity window
  to the architectural default. Select local request fields rather than
  delegating to a package request. Reserved and flexible-valid bits are zero.
- Read back the supported fields. Do not convert performance units to MHz,
  copy the BSP's capabilities to APs, clamp minimum to maximum, change turbo or
  power/thermal limits, or fall back to legacy `PERF_CTL` or AMD controls.

The request uses each CPU's capability range, not machine-specific constants.
The measured Fedora reference also uses autonomous EPP 128, but its requested
minima differ from capability-lowest; Caelum does not reproduce every Linux
field. EPP is omitted when unsupported, without claiming it was configured.

On CPUs advertising HWP control, `IA32_HWP_CTL[0]` can invert the meaning of
request bit 42. Caelum preserves the control register and makes the selector
equal its polarity bit, selecting local fields in either mode. It verifies
defined fields only, including advertised valid bits being zero; reserved
readback bits are not policy. Firmware/PECI overrides and hardware limits can
still constrain the result. Confirmed request fields do not prove delivered
frequency or completion of posted requests outside the logical processor.

## Ownership and results

The BSP and each AP execute their own initialization. There is no heap use,
remote MSR write, scheduler change or new BSP-only service. Per-CPU facts become
immutable before online publication; the BSP reports after acquiring those
publications. `arch/x86_64/hwp.c` retains three outcomes:

| Result | Meaning |
|---|---|
| Unavailable | Vendor or required feature is absent; no HWP MSR is accessed. |
| Applied | Enable and the supported local request fields were confirmed. |
| Failed | An advertised access faulted, the capability range/control mode was unusable, or readback differed. |

One boot summary gives applied/unavailable/failed counts and the number of
applied CPUs lacking EPP. A failure identifies its CPU, stage and MSR; boot
continues without claiming the policy was applied. Failure after enable is
partial state: HWP remains enabled until reset. There is no automatic retry,
software disable, rollback promise or legacy fallback.

Two guarded assembly instruction sites return failure on kernel `#GP(0)`;
the ordinary ISR restores the interrupted state. Other instruction addresses,
user faults and page faults keep their existing handling. This guard is distinct
from the stopped network debugger and does not depend on GS during recovery.

## Limits

No power-mode UI, AC/battery profile switch, dynamic utilization governor or AMD
CPPC/P-state programming is provided. Intel hybrid/cache-aware placement is an
[accepted later direction](../wip/cpu-layout.md), not implemented here. A CPU can
deliver less than the requested maximum because of workload, contention,
firmware or package power/thermal limits; a request is not a measured clock.

Private counter sampling, process tags and log-triggered collection used for
qualification are never part of the production implementation. Native vela A/B
qualification confirms applied state on all sixteen CPUs across three B boots
and removes the large all-load BSP/AP P-core rate split. Original Git/game/UI
reruns remain separate and pending; no battery or energy result is claimed.

Register definitions follow the [Intel SDM, volume 3B, section 17.4](https://cdrdv2-public.intel.com/874250/253669-090-sdm-vol-3b.pdf).
The [Linux intel_pstate reference](https://www.kernel.org/doc/html/latest/admin-guide/pm/intel_pstate.html)
explains why its active `powersave` algorithm is not a fixed minimum clock and
why its maximum-clamped `performance` policy is a different choice.
