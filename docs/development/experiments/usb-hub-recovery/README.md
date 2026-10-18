# USB hub/HID recovery — 2026-10-18

## Cause and accepted scope

The owner observed the ASUS `1ea7:0066` mouse disappear when dock power was
connected on vela. The captured root PORTSC `0x202a0` proves a disconnected
USB2 branch; HID detached and its slots were fenced. Later `0xe03` proved that
the port was live again, but the old HID stayed retired. Replugs and moving the
dock to another controller did not reconstruct its hub/HID path. The older
choppiness is not attributed: the original logs show no repeated retirement,
and the diagnostic boot used a different topology and did not reproduce it.

Source inspection found two independent restrictions: runtime roots admitted
only low/full-speed leaves, and descriptor admission explicitly rejected hub
class 9. Removed HID records were ignored by live collision checks; neither the
native generic refusal nor the first QEMU refusal proved record exhaustion.
NCM privately reconstructed its saved path, excluding ordinary HID siblings.
The captured dock NIC recovery only started; Remote survived through the
separate RTL8156 adapter, so those files do not establish dock-NIC recovery.

Owner accepted on 2026-10-18:

1. One USB physical topology owner admits returning/new supported hubs and HID,
   including another root/prepared controller; NCM uses that same owner.
2. Fresh generations follow proven fences. Retained budgets, bounded retries and
   specific refusal diagnostics remain explicit.
3. Runtime storage stays descriptive: no BOT/raw-block admission or automatic
   pool reattachment.

The [interface](../../../devices/usb-hubs.md#bounded-runtime-topology-recovery)
describes the implementation and limits. There is no public ABI or userland pin
change and no compiler rebuild.

## Before implementation

Current-main control: `f80afa33` (#900 merged), compiler container
`pyxis-llvm23.1.3-3d69e83`. The final matched fixture uses the public image artifact
from [CI 3084](https://git.internal/PyxisOS/pyxis-os/actions/runs/3084), retaining
its exact archive and boot configuration for the changed-kernel run. An earlier
smoke run used the previous clickpad archive; it is not the matched qualification.

QEMU 10.2.2, Q35/KVM/max, four CPUs, 2 GiB, fresh OVMF, VGA 1280x800, VirtIO
network/rng, `qemu-xhci`, full-speed `usb-hub` on bus port 1 with `usb-mouse`
at 1.1, plus an independent high-speed mouse on port 2. QEMU maps those USB2
ports to xHCI roots 5/6. Removing/replacing the whole hub reaches
`USB_RUNTIME_DEVICE`, descriptor class 9, runtime count 1, HID claims 3: explicit
hub-class rejection below either budget. GDB inspected retained fields without
inferior calls or writes. The prior main `30327a46` and daily probe reproduce the
same rejection; #900 did not introduce this admission restriction.

The decisive QMP sequence is:

```json
{"execute":"device_del","arguments":{"id":"hub"}}
{"execute":"device_add","arguments":{"driver":"usb-hub","id":"hub2","bus":"xhci.0","port":"1"}}
{"execute":"device_add","arguments":{"driver":"usb-mouse","id":"mouse2","bus":"xhci.0","port":"1.1"}}
```

Wait for each removal/admission to settle before inspecting it. Record the
actual host pointers and states; reused numeric Slot IDs alone do not show reuse
of old DMA backing.

## Qualification

Ordinary kernel builds pass with the existing compiler. One-CPU source
`54c65bcb` and final four-CPU source `ebbf9cdb` use the same current-main CI
archive; the later code change distinguishes a freshness-held NCM setup from
permanent capacity refusal, a source-reviewed path awaiting native qualification.
There is no diagnostic overlay. GDB makes no inferior calls or writes.

| Check | Result |
| --- | --- |
| Whole-hub replacement (1/4 CPUs) | Fresh hub and mouse admitted; old hosts removed/retiring; numeric slots reused only after fences |
| Motion after replacement | 40 reports on a fresh stream, result `USB_OK` |
| Nested new hub and mouse | Both admitted on the first controller |
| Held-button subtree removal | Delivered source button 1 becomes 0; source inactive and old host removed |
| Previously empty second controller | New hub and mouse admitted; 40 motion reports |
| New high-speed root mouse | Admitted within the existing USB2 HID class profile |
| Runtime storage child | Descriptor configuration selected, `USB_BOT_UNBOUND`; no block/pool binding |
| Retained runtime-record limit | 32/32; new admission refused, both controllers still running |
| Independent operation | Remote completes with exit 0/drain complete; ping gateway 4/4 |

The larger nested/storage/held-button run precedes only removal of the unused
`replaces` member and the conditional NCM setup retry correction. Final-source
hub replacement and capacity/independent-progress checks are repeated separately.
These are functional nested-QEMU observations, not native latency measurements.
QEMU supplies no CDC-NCM peer here; class rebind and real SS/TT hardware remain
native checks. No shared-host USB gadget is created.

Artifacts: `/shared/present/usb-hub-hid-recovery/`; original native logs and
source-inspection findings: `/shared/present/vela-input-native/`. They are runtime
records, not committed diagnostic instrumentation. No USB gadget is used on the
shared development VM.

Native vela qualification remains pending: dock power renegotiation, dongle
replug, moving the dock across controllers, and Remote/ping/integrity over the
existing NCM NIC after same-route recovery. Storage media/pools must not silently
reattach. QEMU's full-speed hub does not qualify real high-speed TT or SuperSpeed
hub hardware. No native throughput or input-latency improvement is claimed.

## Review follow-up — 2026-10-18

Owner accepted the review defaults: connected roots without live records become
pending at runtime start; protect one NCM route-reconstruction share; drain events
during hub waits; cap retries across connection flapping with one visible warning.
The core now separates consecutive failures from total connection attempts.
Success clears the failure streak, not the eight-attempt connection cap. Only
100 ms of observed actual disconnection resets that cap. Fresh NCM leaves count
as unfinished until class setup and final route validation; pre-setup identity
holds retire/retry rather than pretending an unconfigured leaf can be verified.
Three failures or exhausted physical backing stop NCM recovery visibly instead
of leaving it indefinitely pending. Failure backoff survives same-pass retirement.

The prepared core adds up to twelve protected records (two six-component known
routes), deduplicated by physical prefix, with matching host-context protection.
Ordinary 32-record churn cannot consume those credits. A protected attempt,
successful or not, consumes one credit; this does not promise recovery through
arbitrary transient failures. Core, host and HID-claim limits warn once. Relevant
subsystem references and the synchronous-progress comments were corrected.

Ordinary builds pass with the unchanged compiler/configuration. Four-CPU review
code `a463c975`'s predecessor differs only in the NCM same-pass backoff correction
and startup-refusal wording; the final one-CPU run includes both. Matched CI3084
archive, devices and QEMU configuration above remain unchanged. No USB gadget,
new shipped test, inferior call, guest-data write or diagnostic overlay is used.
GDB breakpoints only coordinate ordinary QMP hot-unplug/replacement and inspect
retained state; these are not performance measurements.

| Review check | Observed result |
| --- | --- |
| Four CPUs: returning hub/mouse and previously empty second controller | Fresh active records, both controllers running |
| Four CPUs: independent HID during 120 ms hub power-good wait | Existing interrupt sequence advances 19 → 31, result `USB_OK` |
| Four CPUs: rapid hub replacements without stable disconnect | At eight attempts: failures 0, blocked/warned true; one warning; controller remains running |
| Four CPUs: stable disconnect then reconnect | New connection: attempts 1, failures 0, unblocked; fresh hub/mouse |
| Four CPUs: ordinary record exhaustion | 32 ordinary / 32 total of 44 backing records, no NCM reservation in this fixture; one core warning, host running |
| Four CPUs: independent Remote and network after exhaustion | Client exit 0/drain complete; gateway ping 4/4 |
| One CPU: root connected after boot inspection, before runtime initialization | Boot record count 0; initialization sets pending true, then admits fresh hub and active mouse |
| One CPU: three successive physical admissions interrupted by actual QMP replacement before Address Device | Failure/attempt counts 1/2 then 2/3, finally 3/3 blocked/warned; controller running, not failed |
| One CPU: interrupted admission followed by full success | Failure count 1 → 0; total attempts stays 2 |

QEMU still has no NCM peer: the protected-prefix accounting and incomplete-class
paths have source review, not runtime CDC-NCM coverage. A native result on the
older combined packet cannot qualify these new retry/reservation paths. The
existing owner packet `/shared/present/vela-972-945/` remains byte-for-byte
unchanged at local integration `c04b064c` (#972 `c184a559`, #945 `f1e36fb3`), as
requested. Review evidence: `/shared/present/usb-hub-hid-recovery/review/`.
