# Native paths and working directories

[Libpyxis path helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/path.h) walk the existing directory
protocol in userspace. They use startup scheme-root grants and an explicit
working-directory context. The kernel still accepts only one ordinary component
per lookup; it has no path parser or process-global working directory.

## Syntax and authority

`boot://share/hello.txt` selects the supplied `boot` root. A relative name such as
`hello.txt` starts from the context's current directory. Scheme and component
names are case-sensitive. Unknown schemes fail without falling back to a global
root. Root binding names are nonempty and contain neither `:` nor `/`.

An empty path or a leading `/` is invalid. Repeated separators are accepted;
`.` keeps the current directory. `..` pops a retained directory handle and fails
at the context's boundary. It never asks the kernel for a parent. A trailing `/`
requires a directory. Components are resolved in order: `missing/..` fails at
`missing`, and `file/..` fails because the intermediate object is not a directory.

Only a leading `scheme://` selects a root. There is no URL decoding, wildcard or
environment expansion, query syntax or fragment syntax. Other name bytes are
literal. The shell resolves a bare command by searching `bin://` and then
`boot://` (see [commands](shell.md#commands-and-quoting)); ordinary relative file
lookup does not implicitly search there.

An explicit scheme path starts a new chain with that root as its boundary.
A standalone subtree context cannot escape through `..`; independently supplied
scheme roots remain accessible through their explicit prefixes. The launcher
chooses the initial chain and its order from directory handles it grants. The
kernel validates their types, not their ancestry: those explicit handles are
the authority, regardless of the display string.

An explicit [service namespace](../interfaces/namespaces.md) is separate from directory roots.
Resolution rejects a scheme bound in both routes, including a binding whose
provider has exited. Launch checks known conflicts too; mutation after launch
makes the runtime check necessary. A namespace-only scheme opens files through
its [provider](../interfaces/file-providers.md), passing the complete URI without component
walking, normalization or decoding. The provider decides its own URI semantics.
Provider directories, cwd changes, removal and rename are unsupported; there is
no fallback to directory operations after provider failure.

## Ownership and storage

`path_context_init()` copies a borrowed directory chain into caller-provided
storage, preserving each handle's actual grant independently. Inputs may
come from `startup_working_directories()` or a separately granted subtree.
An empty initial chain is valid; relative lookup then reports UNAVAILABLE until
an explicit scheme change establishes a working directory.

The context owns its copied handles, not its backing array. Keep the storage
alive, do not copy the owning structure, and finish with `path_context_close()`.
Startup handles remain separately owned and may alias other startup bindings;
close each such handle once. The initial display path is an immutable descriptive
snapshot. These helpers do not maintain a displayed path or update environment
variables after changing directory.

Each operation also receives a workspace: a temporary handle array and a buffer
for one component or scheme plus NUL. All storage, input strings and output
locations must be disjoint. There is no library-wide path length or depth limit.
Generic provider OPEN URIs fit the 3,768-byte copied-payload bound; HTTP(S)
URLs fit 2,048 bytes. HTTP provider routes additionally need caller-owned
`provider_http_workspace`, borrowed clock READ authority and an optional earlier
deadline/response destination in `path_workspace`. Libc and ordinary wrappers
allocate that bounded scratch; native path helpers allocate nothing. The
[HTTP bridge](http-fetch.md#redirect-chains) documents chain ownership and limits.
Caller capacity bounds an operation; insufficiency returns LIMIT without
truncation. A later allocator can supply larger arrays without changing the
kernel ABI. Scratch grants are released on every return.

`path_resolve()` returns a new owned handle with exactly the requested kind and
rights. This includes `.` and a bare scheme root. Existing ancestors are copied
without widening their grants; new intermediate lookups request LOOKUP and only
the rights needed to obtain the requested result. Reading a known file never
requires ENUMERATE. READ and WRITE request independent READ_FILES/WRITE_FILES
authority through intermediate directories. Resolving a path does not change
the context.

`path_change()` prepares an entire replacement chain in scratch storage before
closing the old one. Existing ancestors retain their individual grants; each
new child receives the rights queried from its parent handle. The final working
directory must allow LOOKUP. URI spelling never determines access. Missing names,
denied rights, insufficient storage or a failed handle allocation leave the old
context intact. A retained directory
continues referring to its object if its name later changes; displayed text is
not used to rediscover it.

Libc and the shell share the mutable [process working path](process-state.md);
independent native contexts keep their explicit ownership.

## Proved realpath

Libc `realpath` resolves the original input through native capabilities before
normalizing its candidate `scheme://` spelling. It holds the original target,
re-resolves that candidate and requires matching valid live identity domain and
object IDs. Relative input also re-resolves the tracked cwd description and
compares every retained ancestor, with both chains held. Unknown or stale
ancestry and unavailable identity cannot become successful proof. Provider
routes are refused before OPEN; missing components and denied authority remain
real errors. `missing/..` still fails. Aliases keep their selected scheme names.

The bounded profile accepts input and output fitting `PATH_MAX` (4096 bytes
including NUL), without imposing that bound on other path helpers. A supplied
result buffer must hold PATH_MAX bytes and stays unchanged on failure;
`realpath(path, NULL)` allocates a result released with `free`. Libuv's synchronous
`uv_fs_realpath` owns its proved result until request cleanup. Proof samples
identity while references are held, freezes no names or contents and promises
nothing about a later lookup after return. A descriptive `getcwd` is not a proof.

## Removal paths

`path_remove()` walks intermediate components in order, retaining parent grants,
and sends one REMOVE for the final name. FILE, DIRECTORY and ANY select the
allowed child kind. A trailing separator requires a directory; FILE with a
trailing separator fails without removal. Bare scheme roots and final `.`/`..`
are rejected. An intermediate `missing/..` still fails at `missing`.

The parent needs REMOVE; intermediate traversal needs LOOKUP and authority to
obtain that parent grant. Child file READ/WRITE and enumeration are unnecessary.
The helper uses caller-owned scratch storage and closes every temporary handle.
It never removes by a displayed path or by a child handle.

## Rename paths

`path_rename()` resolves source and destination parents using separate caller-owned
workspaces, then performs one native file rename. Both parents remain held until
the call completes; no child is opened or probed first. Source and destination
are exact file paths. Roots, final `.`/`..`, trailing separators and directory
moves are rejected. Intermediate components retain normal ordered traversal and
boundary checks. Every temporary handle is closed; the context is unchanged.

Source traversal requests REMOVE. Destination traversal requests CREATE and,
for replacement, REMOVE. If the latter traversal is denied it retries with
CREATE alone, permitting a new destination under a restricted grant. The kernel
still decides whether replacement is authorized at publication; this retry does
not probe destination existence or widen any capability.

## Local handle copies

[Handle copying](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/handle.h) installs another reference in the
same process. `handle_copy()` preserves both resource and transport authority;
`handle_copy_restricted()` requests an exact subset of each mask, including zero. Neither changes the source or grants extra
authority. Closing either copy leaves the other alive.

The COPY syscall validates its output buffer before insertion. A full table uses
the existing BSP growth request; the source slot retains the object throughout
the loan, and no table-entry pointer survives growth. Failure leaves the source
and existing table entries intact and publishes no new handle. The wrapper clears
its output on failure. This is local copying, not an endpoint attachment or move.

## Consumer and failures

The optional Hello example starts with `boot://` as its working-directory boundary. It enumerates the
application tree, reads `boot://share/hello.txt`, changes into `boot://share`, and
reads `hello.txt` relatively. It releases discovered handles and the context
before closing its original startup grants.

Helpers return native call statuses: BAD_REQUEST for malformed syntax, NOT_FOUND
for an unknown scheme or missing component, WRONG_TYPE for a kind mismatch,
UNAVAILABLE for absent working-directory context, DENIED for a boundary escape
or insufficient rights, and LIMIT for insufficient caller storage. Kernel
allocation and handle-limit failures pass through. Libpyxis does not allocate
workspace storage or translate failures into errno. Executable search policy
remains with the caller.
