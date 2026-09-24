# Userspace URI scheme providers

Status: agreed design direction for later work, not an implementation task or a
frozen ABI. This does not expand the current loopback/IPv4 milestone. It builds
on the [namespace direction](../vfs.md) and [named endpoints](../spaces.md#named-endpoints).

## Intended use

A userspace service could expose resources through a URI scheme, allowing:

```c
FILE *file = fopen("http://example.com/hello.txt", "r");
```

and ordinary tools to consume the same resource:

```text
cat http://example.com/hello.txt
```

The provider implements fetching; a separate download utility is unnecessary
for this simple read. This does not eliminate the HTTP client implementation
or provide every option of a tool such as curl.

Both HTTP and HTTPS clients belong entirely in userspace. HTTP parsing, headers,
redirects, TLS, certificate verification and response caching stay there. The
kernel supplies network primitives, IPC, capability enforcement and namespace
binding/routing. It must not fetch URLs or contain an HTTP/TLS implementation.

## Binding, routing and authority

An authorized application registers a provider endpoint under a scheme within
a namespace. Init can establish default bindings and delegate a namespace to a
space or process. Registration and replacement require explicit authority; there
is no unrestricted global registration table.

Opening a URI selects a binding in the caller's namespace and routes an open
request to that provider. The provider returns an opened-resource capability or
an error. Libpyxis/libc adapts that resource to the file interface, including
`FILE *`. Kernel routing only needs the generic scheme/binding contract; the
provider interprets the scheme-specific address and protocol.

Knowing a URI does not authorize access. The caller needs the applicable binding
and operation rights. The provider's own network grants limit its access, but
are not a substitute for enforcing caller policy: a shared privileged provider
must not turn a restricted caller's request into unrestricted network access.
Define how those restrictions are represented and checked before implementation,
including redirects and cache hits.

Changing a binding affects subsequent resolution, not already opened resources.
Existing handles retain their original object/provider relationship. Provider
exit must produce a defined closure/error result and wake blocked clients;
it does not silently rebind their handles to a replacement service. Registration
ownership, namespace inheritance, unregistration, restart and explicit revocation
still need concrete lifetime rules.

The current URI path helpers resolve directory roots in userspace. Kernel-managed
provider bindings would be a new facility, not a description of existing code.
Keep ordinary directory traversal and the provider-open contract distinct; an
HTTP URI need not be represented as a tree of remote directory objects.

## Initial HTTP size policy

The initial provider rejects response bodies whose length is not known upfront.
It does not download an unknown-length body merely to discover its size. This is
an intentional compatibility restriction, not a claim that such HTTP responses
are invalid or always belong to streaming applications.

Finite responses can omit `Content-Length`. HTTP/1.1 chunked framing carries chunk
sizes rather than an upfront total; connection-close framing is also possible.
There is no universal streaming flag. Reject chunked and close-delimited bodies
in the initial subset, and reject conflicting framing rather than trusting a
length beside `Transfer-Encoding`.
See [HTTP/1.1 body framing](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3).

For a body-bearing GET response in the initial subset, require a valid
`Content-Length` within the configured resource limit. Zero is a valid declared
length. Interpret method/status semantics before length; a separate HEAD request
is not proof of the length of a later GET. Parse lengths with overflow checks.
See [Content-Length semantics](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.6).

The exposed file size must describe the bytes the caller reads. Content decoding
must not confuse the transmitted encoded length with the resulting file length.
A simple initial option is to request identity encoding and reject unsupported
content encodings; settle that exact policy before implementation.

A declared length is a bound to verify, not permission for unchecked allocation.
Set response-size budgets and receive deadlines. Incomplete transfers must fail,
not become successful shorter files or cached results. Invalid framing is an
error. The detailed HTTP status/redirect mapping remains undecided.

Known size does not provide random access. Current native files use explicit
read offsets and size queries, so decide whether the initial provider stages a
complete bounded body before returning a read-only file, or implements another
honest offset-read contract. Do not assume servers support range requests or
fetch a potentially different representation for each read. Temporary storage
for one open is separate from retaining responses for reuse across opens.

## Future response cache

A later userspace cache may retain complete responses for a limited time and use
LRU to evict entries under a byte budget. LRU selects what to discard; freshness
policy determines whether retained data may satisfy a new open. Retain only
completed, validated bodies. Cache capacity, maximum object size, lifetime caps
and accounting remain explicit policy choices, not kernel HTTP behavior.

Reuse must respect HTTP caching rules, including `Cache-Control`, validators,
`Vary`, and restrictions on authenticated/shared responses. A local time limit
must not authorize reuse forbidden by the response. Consult
[HTTP caching](https://www.rfc-editor.org/rfc/rfc9111.html#section-4) when defining
this milestone. Namespace/authority and credentials belong in the cache's
isolation policy; a URL alone is not a sufficient sharing boundary.

Eviction removes reuse eligibility; it must not invalidate storage still owned
by open handles. Account for that retained storage too. Unknown-length responses
remain rejected unless that policy is explicitly changed in a later milestone;
adding a cache does not implicitly enable them.

## Compiler experiment

A future compiler could resolve:

```c
#include "https://example.com/foo.h"
```

through its granted namespace. Compiler include lookup would need URI handling,
relative includes based on the containing resource, and defined file identity.
`fopen` support alone does not guarantee that an unchanged compiler accepts or
resolves these names correctly. The caller still needs provider authority.

Direct fetching could serve interactive experiments. Repeatable builds should
pin content and use controlled dependency storage; a time-limited response cache
does not itself make a moving URL reproducible. No compiler or toolchain changes
are assigned by this idea.

## Prerequisites and decisions

Before an implementation milestone, settle:

- Registration/lookup requests, namespace scope and delegation, binding lifetime,
  provider death and how policy follows a routed request.
- Reply-side capability transfer and resource ownership. Current
  [endpoints](../endpoints.md#copying-a-capability) transfer a capability with a
  request only; returning a new handle requires real transfer support, not a
  numeric handle embedded in reply bytes.
- URI/request transport beyond the current endpoint's small inline payload,
  server-side file resources or bounded materialization, timeouts, cancellation
  and resource accounting. No private pointers may cross process boundaries.
- TCP, name resolution for hostnames, the userspace HTTP implementation, HTTPS
  library and trust configuration. TLS stays out of the kernel regardless of
  library choice.
- HTTP status mapping, redirects, encoding, size/deadline limits and offset-read
  behavior for the first read-only provider. Cache and compiler work come later.

This is a future consumer of networking, IPC and namespace work. It is not a
reason to add placeholder syscalls, provider registries or protocol adapters to
the current networking tasks.
