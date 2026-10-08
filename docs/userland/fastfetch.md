# Fastfetch

The normal image includes Fastfetch 2.69.0 at `bin://fastfetch.pxe`. The shell
resolves the bare command `fastfetch`. Its upstream MIT license, bundled yyjson
notice, MPL-2.0 license and native port notice are installed under
`boot://share/licenses/fastfetch`.

```text
fastfetch
fastfetch --json
fastfetch --config home://fastfetch.jsonc
fastfetch --logo none | cat
fastfetch --json > home://system.json
```

The default text display uses the owner's Pyxis compass-rose ASCII logo and eight
information modules; Battery prints nothing on a machine without a battery. Colors, Break and Separator are also available through
upstream module selection. Explicit JSON/JSONC files use ordinary libc reads
through the caller's namespace; no configuration is discovered automatically.
JSONC accepts comments and trailing commas. CLI presentation overrides and
upstream module formats remain available. The [recipe reference](../../ports/fastfetch/README.md)
contains configuration examples, source pin, patch order and standalone build
instructions.

## Native observations

| Module | Source and meaning |
| --- | --- |
| OS | Pyxis OS name and architecture from `system_info` READ |
| Kernel | Caelum name, architecture and running kernel source commit; ABI page size |
| CPU | Cached guest-visible BSP brand and online logical CPU count |
| Memory | **Memory (allocator)**: allocator total and allocated bytes |
| Uptime | Monotonic duration since HPET initialization through clock READ |
| TerminalSize | Columns/rows from the named output console |
| Disk | Observable native root bindings and labeled shared-pool capacity |
| Battery | Percentage, AC and charge status, time remaining, cycle count and identification from `system_info` READ |

[System information](../interfaces/system-information.md) defines the CPU,
identity and coherent memory replies. These queries do not form one atomic
snapshot. Allocator total excludes permanent reservations and is not installed
RAM; online CPU count is not physical cores or process allowance. Uptime omits
time before HPET initialization and does not establish a calendar boot time.

Battery reads the kernel's latest ACPI poll, at most about five seconds old,
through the [power queries](../interfaces/system-information.md#power-and-batteries).
It shows each inserted battery with a known percentage; manufacturer, model,
technology and serial are the firmware's OEM, model, type and serial strings.
While discharging, time remaining is remaining capacity over the present rate,
as Linux computes it. Temperature and manufacture date are unavailable. Power
Adapter is not built, since ACPI reports no adapter wattage; `[AC Connected]` in
Battery's status shows AC presence.

Unavailable optional fields retain upstream empty/unset formatting and JSON null
values. For example, `{name}{?freq-max} @ {freq-max}{?}` omits unknown frequency;
uptime's `bootTime` is null and calendar placeholders are unset. Native query
failures use upstream module diagnostics. A module error does not guarantee a
nonzero process exit status; allocation and assertion behavior remain upstream.

Actual stdout binding selects automatic plain output. Redirected text may retain
the ASCII logo; `--logo none` suppresses it. The named output grant can still
supply dimensions while stdout is a file or pipe. JSON omits terminal setup and
logo output. Narrow terminals retain upstream layout, so long lines may wrap;
use a shorter format or disable the logo when needed.

The port excludes automatic discovery, config/cache generation, dynamic refresh,
image logos, Lua/JS formats, threads and executable/network helpers. Broader
upstream help remains compiled in; excluded requests follow the documented
unsupported diagnostics or upstream fallback behavior. See
[remaining limits](../technical-debt.md#fastfetch-first-port-boundary).

## Native Disk observations

Disk uses only the caller's explicitly selected startup directory bindings.
It queries native directory handles carrying FILESYSTEM_INFO; archive/RAM/HOST,
exported providers and roots without observation authority are omitted. An actual
handle/query failure uses upstream module diagnostics rather than inventing an
empty filesystem or a successful partial list. No mount, raw-device or principal
acquisition service is used. Existing shell/session/remote handoffs already carry
the selected root grants with their actual rights.

The filesystem type is displayed as `npfs`; `hideFS` uses that same name.
The default text identifies each binding, filesystem type and read-only state,
with an explicit **shared pool capacity** label. Multiple volumes or repeated
mounts of one pool may produce separate rows with the same capacity; they share
one pool identity and must not be summed. Binding names come from the caller's
namespace, independently of filesystem volume names.

The [scoped query](../interfaces/directories.md#scoped-filesystem-information)
provides pool/volume identity, volume name, retained generation, separate
GPT/filesystem degradation and allocatable pool bytes. Capacity excludes the two
superblock slots but includes shared metadata and reserves. It is neither a
volume's writable allowance nor a usage measurement. Disk leaves standard volume
byte totals, used/free/available bytes, file counts, creation time, ages and
percentages unavailable. JSON uses null and custom formats receive unset values;
percentage/bar options cannot manufacture usage. Separate native fields expose
the retained pool observation. Ordinary opening checks geometry and root
envelopes, without a full consistency scan. Each JSON Disk row adds a `native` object with
`poolId`, `volumeId`, `selectedGeneration`, `poolAllocatableBytes`, `gptDegraded`
and `filesystemDegraded`. IDs are 32 lowercase hex digits in filesystem byte
order. `name` is the volume name; `mountpoint` is the caller's binding;
`mountFrom` is unavailable.

Custom format additions are `{pool-id}`, `{volume-id}`, `{generation}`,
`{pool-capacity}` (formatted size), `{pool-capacity-bytes}`, `{is-gpt-degraded}`
and `{is-filesystem-degraded}`. For example:

```text
fastfetch --logo none --structure Disk --disk-format '{mountpoint}: {filesystem} [shared pool: {pool-capacity}]'
```

Unavailable standard placeholders remain unset, including numeric slots and
conditional expressions. Native custom-key link placeholders are unset rather
than manufacturing `file://` links for capability bindings. Unix folder/glob
filters are unsupported; requesting them produces a module diagnostic. Their
colon-separated path grammar is not a native binding selector. Upstream text
volume-type filters and JSON behavior remain unchanged.

With no observable native roots, upstream text reports `No disks found` when
errors are enabled and JSON returns an empty Disk result. A disk may be attached
but intentionally hidden by mounting with `--no-info`. This does not hide content
from a separately granted read capability or authorize Fastfetch to reacquire it.

## Build and integration evidence

`make -j16 image` builds the pinned recipe through the SDK and packages its
executable and notices using the ports install manifest. It needs host CMake
3.21 or newer and the existing compiler; no compiler-container, kernel or libc
change is required for image integration. The upstream formatter, containers,
dispatch, diagnostics and three native-port patches were unchanged by packaging.

Integration was exercised on 2026-09-30 with GCC 16.2.0, CMake 3.31.8, SDK
userland `c9ed311`, and kernel source `9bbb190091d5`. The ordinary image build
passed. Interactive QEMU 10.2.2 with the documented AHCI fix used four CPUs,
CPU `max`, nested KVM, 256 MiB, Fedora OVMF, entropy, virtio-net and a private
virtio-fs export. Fastfetch ran from the boot archive; the export supplied only
configuration, captured output and a disposable launcher.

Local 160x48 and remote 100x30/40x12 invocations completed successfully and
reported their corresponding dimensions. The local logo rendered in full;
narrow output retained long upstream layout lines. Explicit JSONC formatting,
file redirection and a `cat` pipeline worked. Captured plain text and JSON had
zero escape bytes; host parsing confirmed the six JSON results and null fields.
Three separate JSON invocations completed successfully with advancing uptime
and the same allocator total; memory usage changed with other process lifetimes.
This is a functional repeated-run check, not a leak or performance measurement.

GDB compared the running packaged command with its native sources:

- CPU matched the kernel's cached `12th Gen Intel(R) Core(TM) i9-12900K` brand
  and four online CPUs.
- PMM reported 48,747 total and 10,330 allocated frames. The native reply and
  captured JSON both contained 199,667,712 total and 42,311,680 used bytes.
- A clock result of 32,270,385,590 ns became 32,270 ms in the uptime adapter
  and JSON, with no inferred boot timestamp.

A disposable launcher passed only memory and stdout/stderr authority to the
packaged executable. All six data modules produced JSON errors for missing
system_info, clock and named output grants; the process exited zero, matching
upstream behavior. The logo source hash matched the owner's ASCII file. No tests,
fault injection or boot automation were added. These are nested-VM observations;
all validation processes were stopped afterward.

## Native Disk validation

Task 7 was validated on 2026-09-30 against SDK userland `3b9ba3f` and kernel
`98e6557`, with GCC 16.2.0 and CMake 4.4.3. A fresh standalone recipe and ordinary
`make -j16 image` passed. Interactive QEMU 10.2.2 with the documented AHCI fix
used four CPUs, CPU `max`, nested KVM, 256 MiB, Fedora OVMF, virtio-net, a private
virtio-fs export and a read-only virtio-blk attachment with 512-byte sectors.

The host checker accepted both retained states of the populated image before
attachment. Init mounted `system` as `data://` and `mirror://`, `headers` as
`headers://`, and another `system` binding as `private://` with `--no-info`.
Local 160x48 and remote 100x30 output showed the three observable bindings;
archive, RAM, HOST and `private://` were absent. The default seven data modules
and logo still worked. A `cat` pipeline and file redirection preserved Disk text.

All three rows reported pool `22743030ffd60d729828bb94719d8bc4`, generation 1
and 67,100,672 allocatable bytes (63.99 MiB). The `system` volume ID was
`d548c7c629d503ad7e844807b3ffa498`; `headers` was
`3140fec750881c48cbda331fe954d362`. No capacity sum was printed. GDB observed
a successful live Fastfetch query with that generation/capacity and only the
read-only flag set. Host metadata and captured JSON agreed.

Explicit JSONC exercised named and numeric format slots, optional-field
conditions and custom keys. Unavailable usage, file counts, time/age, percentages
and links were empty; JSON values were null. Native values remained available.
Captured text/JSON had zero escape bytes, and a repeated Disk JSON invocation
was identical. Requested folder filters returned the documented module error;
`hideFS: ["Pyxis"]` returned no rows. `showReadOnly: false` hid text rows while
JSON retained them, matching upstream behavior.

A separate boot delegated only the `--no-info` native binding: Disk returned
an empty JSON result and `No disks found` with errors enabled, while ordinary
`cat` still read its welcome file. The ordinary default image, with no disk or
mount configuration, returned the same empty Disk result; all seven data modules
completed without JSON errors. After detaching the image its SHA-256 was unchanged.
All QEMU, debugger, remote-client and virtiofsd processes were stopped.

Degraded-health rendering, grant/query failure cleanup and allocation failure
were reviewed in source rather than induced at runtime. Repeated execution is
a functional check, not a leak/performance measurement. These observations are
from nested KVM. The later [combined workflow validation](../devices/native-readonly-filesystem.md#combined-workflow-validation)
closes the native mount milestone.
