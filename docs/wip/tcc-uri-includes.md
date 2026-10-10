# TCC includes by URI

Status: **proposal, 2026-10-10; three owner decisions open.** This is the
[compiler experiment](userspace-scheme-providers.md#compiler-experiment): TCC
resolving `#include "https://…"` and other `scheme://` names through the caller's
granted namespace. Nothing below is implemented unless it is listed under what
already works.

The motivating cases are a raw GitHub gist, such as RabaDabaDoba's
`ANSI-color-codes.h`, and `stb_image.h` from `raw.githubusercontent.com`, each
pinned to a revision.

## What already works

The [TCC port](../userland/tcc.md#resources-and-paths) treats any
`scheme://NAME` as rooted and opens it directly with `fopen`, and libc opens
provider URIs through the [file-provider bridge](../interfaces/file-providers.md).
QEMU on main `4e7343cd` (nested KVM, 4 CPUs, user-mode NAT networking, unchanged
image) showed:

| Case | Result |
| --- | --- |
| `#include "https://gist.githubusercontent.com/…/raw/<revision>/ANSI-color-codes.h"` in a local source | compiled; colours printed |
| `tcc https://gist.githubusercontent.com/…/raw/<revision>/testmain.c` | its `#include "ANSI-color-codes.h"` resolved beside the URL; ran |
| `#include "home://h5.h"`, `#include "boot://sdk/usr/include/stdio.h"` | compiled and ran |
| A fetched header including `"../b.h"` | worked, but `GET /inc/sub/../b.h` went to the server unresolved |
| A fetched header with `#pragma once` | error: "#pragma once comparison requires file identity" |
| A 404 for an explicit URI | "include file '…' not found" |
| A refused connection | "could not open …: Input/output error", then "not found" |
| An unknown host | "not found" |
| A fetched header's quoted include that the server lacks | 404, then found in a local `-I` directory |
| `-I http://host/inc` | worked; compiling a file that includes `b.h` and `stdio.h` also requested `stdio.h`, `stdarg.h`, `stddef.h`, `sys/types.h` and `stdint.h` from the server |

Include guards work because TCC skips a guarded header by name before opening it.
Each open fetches a new [snapshot](../userland/http-fetch.md): bounded by 16 MiB
and 30 seconds, following at most ten redirects, with no cache.

## Proposed behaviour

A small ninth TCC patch, `0009-uri-includes.patch`, changes only include
resolution and diagnostics. libc, libpyxis, the providers and the kernel stay as
they are.

### Names

- **Rooted names stay rooted.** `"scheme://NAME"` and `<scheme://NAME>` are
  opened as given, never prefixed with an include directory, in both forms.
  `#include_next` keeps requiring a relative name. This is today's behaviour.
- **Every scheme is equal.** `home://`, `boot://`, `host://`, `tmp://`, `http://`
  and `https://` work the same way in `#include`, `-include` and as command-line
  sources. TCC learns no scheme names. A URI is network-like only in the
  resolution rule below, which applies when the containing file is an `http://`
  or `https://` snapshot.

### Relative includes inside a fetched header

A quoted relative include inside an `http(s)://` header resolves against that
header's **final URL** (after redirects, from `pyxis_stdio_response`) by
[RFC 3986 section 5.2](https://www.rfc-editor.org/rfc/rfc3986.html#section-5.2):
merge with the base path, remove `.` and `..` segments, and drop the base's query
and fragment. `"../b.h"` from `https://h/inc/sub/a.h` fetches `https://h/inc/b.h`.
Native roots keep today's unnormalized traversal, where `missing/..` must still
fail at `missing`.

C's search order is unchanged: beside the including file first, then the include
directories. A remote header's `#include "config.h"` can therefore be satisfied
by a local `-I` directory after a 404.

### Failures

Only *not found* continues the search. That covers ENOENT, which includes an HTTP
404 or 410 and an unknown host. Every other failure from a URI candidate stops
compilation with the cause, instead of silently falling back to a local header
of the same name:

```text
host://tu/c.c:1: error: could not fetch 'https://h/x.h': Operation timed out
host://tu/c.c:1: error: include file 'https://h/x.h' not found
```

A failed `fopen` reports only errno, so the HTTP status is not printed; a later
libc change could expose it. Native paths keep their current search behaviour.

### Authority

TCC stays a plain program at `bin://tcc.pxe`. Shell children receive the space
namespace with LOOKUP, so TCC can open `https://` exactly when `cat https://…`
can, under the caller's providers and network. No grant is added. A future TCC
bundle would have to request the provider in its manifest, like any other bundle.

### Pinning and repeatability

TCC adds no cache and no lock file. A build is repeatable when its URIs are
immutable:
- `raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>`;
- a gist's `raw/<revision>/<file>`;
- a gist's per-file `raw/<blob>/<file>`, whose segment is the file's git blob ID.

A branch name such as `master` moves, and so does the build. TCC cannot tell the
two kinds apart and does not warn. `tcc -E` output records exactly what was
compiled, with URIs kept in its line markers.

## Decisions for the owner

1. **`#pragma once` on fetched headers.** Snapshots have a domain but no object
   ID, so the [native identity rule](../interfaces/file-metadata.md) cannot
   compare them.
   - **Default:** within one translation unit, two `http(s)` headers are the same
     when their final URLs are equal after RFC 3986 normalization (scheme and
     host case, default port, dot segments, no fragment). A once-marked URI named
     again is skipped without a fetch. A different name is fetched, and skipped
     if it redirects to the same final URL. The first snapshot stays open, as
     once headers do today.
   - **Alternative:** keep the error, and require include guards in remote
     headers.
2. **Remote include directories (`-I https://…`).**
   - **Default:** refuse a URI include or library directory with "include
     directories must be local; name remote headers in #include". A remote
     directory makes every system-header search a request: five extra in the
     observed run, each revealing a header name to the server and costing up to 30
     seconds when the network fails.
   - **Alternative:** keep allowing it.
3. **Pinning.**
   - **Default:** no cache and no integrity syntax. Immutable URIs are the pin,
     and the docs show the GitHub and gist forms above.
   - **Alternative:** TCC verifies an optional `#sha256=<hex>` fragment on an
     include URI before using the bytes. Providers never transmit fragments.
     This needs a SHA-256 in the patch and a decision on the hash spelling.

## First task after acceptance

The TCC patch, with its README and [TCC reference](../userland/tcc.md) updates.
QEMU demo with user-mode NAT networking: compile and run a local program
including the revision-pinned ANSI gist header over HTTPS, and
`tcc <revision URL>/testmain.c`. Then compile `stb_image.h` at a pinned commit
with `STB_IMAGE_IMPLEMENTATION`, using `STBI_NO_*` options if TCC lacks something
it needs.

The resolution, failure and `#pragma once` rules are checked against a local
host HTTP server. Native runs follow in an owner batch.
