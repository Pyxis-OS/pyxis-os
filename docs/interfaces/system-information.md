# Native system information

The explicitly delegated `system_info` resource supplies three synchronous
queries through [the system-information ABI](../../include/abi/system_info.h).
One READ right authorizes system-wide identity, CPU and allocator observations.
There is no ambient query or acquisition syscall. Kernel bootstrap creates the
stateless authority for trusted init; ordinary local and remote launch paths
forward it explicitly. Restricted launches can omit it, and shell-launched
service providers do not receive it.

## Queries and meaning

All requests contain only a `message_header`. Replies are bounded records with
fixed-size NUL-terminated strings and zeroed unused bytes.

| Query | Reply | Meaning |
| --- | --- | --- |
| `SYSTEM_INFO_IDENTITY` | `system_info_identity` | `Pyxis OS`, `Caelum`, `x86_64`, and the source commit embedded in the running kernel |
| `SYSTEM_INFO_CPU` | `system_info_cpu` | Guest-visible BSP brand and online logical CPU count after successful SMP boot |
| `SYSTEM_INFO_MEMORY` | `system_info_memory` | Coherent allocator total, allocated and free bytes |

Empty build-revision or CPU-brand strings explicitly mean unavailable fields;
other fields remain usable. CPU count is neither physical cores nor the CPU
allowance of a pinned process. The BSP brand is sampled once, not an inventory
of heterogeneous CPUs or an identification of the physical host. CPU hotplug
is not supported.

Memory is **Memory (allocator)**: capacity excludes permanently reserved frames,
and `total_bytes = allocated_bytes + free_bytes`. This is neither installed RAM,
Linux-style available memory nor process RSS. All holders of READ can observe
these global counters. Each memory query uses the existing
[BSP executor](../kernel/bsp-service-requests.md) with interrupts disabled, keeping
the PMM's ownership contract. It samples all counters together without memory
mutation, a new allocator lock or an AP read of allocator state. The caller
copies the reply after the request returns ownership and releases its storage.

Identity and CPU data are immutable before scheduler startup publishes them to
user tasks; their queries need no BSP request. The object retains no process or
user-buffer pointer. Ordinary object references govern its lifetime and BSP
retirement frees it after its final reference ends.

## Errors and library interface

Wrong protocol or operation is `CALL_BAD_OPERATION`; missing READ is
`CALL_DENIED`; a request payload or undersized reply capacity is
`CALL_BAD_REQUEST`; invalid user storage is `CALL_BAD_BUFFER`. Invalid or omitted
handles follow the ordinary `CALL_BAD_HANDLE` path. No authority failure returns
synthetic observations.

Libpyxis exports `system_info_get_identity`, `system_info_get_cpu` and
`system_info_get_memory` through `<system_info.h>`. Pass an explicitly supplied
handle, typically `startup_resource("system_info")`. The wrappers validate the
reply and leave the caller's output unchanged on every failure. The SDK exports
both the ABI and library headers with the ordinary runtime build.

Uptime remains on [clock READ](../../include/abi/clock.h), whose monotonic epoch
begins at HPET initialization and omits earlier boot time. It makes no wall-time
guarantee across VM pauses or suspend. Dimensions remain on the application's
[console capability](../../include/abi/console.h), including remote terminals'
fixed dimensions. Neither is part of an atomic system-information snapshot.

## Build identity

Normal source builds embed `git rev-parse --short=12 HEAD` from the kernel's
checkout. A longer abbreviation can be returned when needed for uniqueness.
A generated header is refreshed on every Make invocation; only its content
change rebuilds the identity object. This follows branch/commit changes in
linked worktrees as well as ordinary checkouts. A missing Git revision becomes
an empty string; a source archive inside another checkout does not inherit the
outer repository's identity. Prebuilt kernels retain their embedded revision.

This identifies the base source commit, not a clean-tree attestation. No `-dirty`
suffix or input-filtering policy is implemented. Documentation and dependency
commits can change HEAD without changing kernel behavior. SDK or userland
revisions are not substituted for the running kernel's revision.

The [standalone Fastfetch port](../../ports/fastfetch/README.md) consumes this
interface. Default-image integration remains in [the port milestone](../wip/fastfetch.md);
no permanent diagnostic command was added. This interface needs an updated SDK
and userland build, not a compiler-container rebuild.
