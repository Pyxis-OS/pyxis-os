# Native paths and working directories

[Libpyxis path helpers](../userspace/include/path.h) walk the existing directory
protocol in userspace. They use startup scheme-root grants and an explicit
working-directory context. The kernel still accepts only one ordinary component
per lookup; it has no path parser or process-global working directory.

## Syntax and authority

`app://share/hello.txt` selects the supplied `app` root. A relative name such as
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
literal. Bare command lookup under `app://` is future shell policy; ordinary
relative file lookup does not implicitly search there.

An explicit scheme path starts a new chain with that root as its boundary.
A standalone subtree context cannot escape through `..`; independently supplied
scheme roots remain accessible through their explicit prefixes. The launcher
chooses the initial chain and its order from directory handles it grants. The
kernel validates their types, not their ancestry: those explicit handles are
the authority, regardless of the display string.

## Ownership and storage

`path_context_init()` copies a borrowed directory chain into caller-provided
storage, requesting the chosen directory rights on each handle. Those rights
include LOOKUP and bound grants retained by later directory changes. Inputs may
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
Caller capacity bounds an operation; insufficiency returns LIMIT without
truncation. A later allocator can supply larger arrays without changing the
kernel ABI. Scratch grants are released on every return.

`path_resolve()` returns a new owned handle with exactly the requested kind and
rights. This includes `.` and a bare scheme root. Existing ancestors are copied
without widening their grants; new intermediate lookups request LOOKUP and only
the rights needed to obtain the requested result. Reading a known file never
requires ENUMERATE. Resolving a path does not change the context.

`path_change()` prepares an entire replacement chain in scratch storage before
closing the old one. Missing names, denied rights, insufficient storage or a
failed handle allocation leave the old context intact. A retained directory
continues referring to its object if its name later changes; displayed text is
not used to rediscover it.

## Local handle copies

[Handle copying](../userspace/include/handle.h) installs another reference in the
same process. `handle_copy()` preserves rights; `handle_copy_restricted()` requests
an exact subset, including zero. Neither changes the source or grants extra
authority. Closing either copy leaves the other alive.

The COPY syscall validates its output buffer before insertion. A full table uses
the existing BSP growth request; the source slot retains the object throughout
the loan, and no table-entry pointer survives growth. Failure leaves the source
and existing table entries intact and publishes no new handle. The wrapper clears
its output on failure. This is local copying, not an endpoint attachment or move.

## Consumer and failures

Hello starts with `app://` as its working-directory boundary. It enumerates the
application tree, reads `app://share/hello.txt`, changes into `app://share`, and
reads `hello.txt` relatively. It releases discovered handles and the context
before closing its original startup grants.

Helpers return native call statuses: BAD_REQUEST for malformed syntax, NOT_FOUND
for an unknown scheme or missing component, WRONG_TYPE for a kind mismatch,
UNAVAILABLE for absent working-directory context, DENIED for a boundary escape
or insufficient rights, and LIMIT for insufficient caller storage. Kernel
allocation and handle-limit failures pass through. No libc errno translation,
heap, filesystem mutation or executable search policy is introduced here.
