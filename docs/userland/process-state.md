# Working path and environment

Libc owns one mutable working-path context and one copied environment per process.
They are single-threaded userspace state, seeded lazily from the immutable
[startup snapshot](../../userspace/include/startup.h). The kernel has no cwd or
implicit environment inheritance. Native code can still use independent
[capability path contexts](paths.md).

`chdir` preallocates a replacement chain, description and traversal workspace,
then calls `path_change` before publication. Failure preserves cwd and rights.
All libc path operations use this context; existing open files keep their own
handles. Root/namespace overrides borrow caller-owned bindings through
[`pyxis_working_bindings`](../../userspace/libc/include/pyxis/working_path.h).
The shell uses the same context rather than owning a second cwd.

`getcwd` returns the tracked normalized `scheme://` description. It does not
prove physical ancestry or confer authority; external rename can leave the text
stale while retained directories remain valid. Unknown launch spelling returns
ENOTSUP; relative operations still use the real chain, and an explicit scheme
change establishes a description. A short buffer returns ERANGE unchanged.
`getcwd(NULL, 0)` allocates an exact-sized result released with `free`.
`realpath` supplies a separately [proved bounded spelling](paths.md#proved-realpath);
`getcwd` remains descriptive even when such proof fails.

`getenv`, `setenv` and `unsetenv` use copied strings. Names are case-sensitive,
nonempty and exclude `=`; empty values differ from absence. `overwrite == 0`
preserves an existing value, and removing an absent name succeeds. Failure
preserves existing values. Borrowed `getenv` pointers must not be modified or
freed and may be invalidated by mutation. There is no writable `environ` or
`putenv`. Native [`pyxis_environment_get`](../../userspace/libc/include/pyxis/environment.h)
distinguishes absence from failed initialization so configuration cannot silently
fall back after allocation failure.

Owned cwd/environment snapshots survive later parent mutation and remain alive
through synchronous launch capture. Stock launch adapters forward current state
explicitly by default; a supplied environment replaces it, including an empty
one. Libuv resolves an explicit child cwd in an independent temporary context,
including relative executable lookup there. Child mutations do not affect the
parent. Deliberate restrictions remain: remote shells start at `tmp://`, network
configuration overlays selected variables, and grants are explicitly attenuated.
The generic launcher still consumes only the supplied request.

Snapshots close once. Native cwd clear prevents bootstrap-chain resurrection;
the session daemon clears retained cwd before releasing its original bootstrap
grants. The mutable state adds no directory authority. Lua, libuv, Fastfetch,
TCC, Links and EDuke32 use this shared state. Links keeps relative file URLs to
avoid replacing a retained cwd with a reopened descriptive spelling.

[Qualification and launch costs](../development/experiments/process-state/README.md)
separate QEMU results, source inspection and remaining limits.
