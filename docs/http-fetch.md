# Userspace HTTP fetching

Task 8 of [userspace services](wip/userspace-services.md) supplies a bounded,
plain HTTP fetch library and a native development consumer. Task 9 will connect
it to provider OPEN and exported snapshot files. There is no `http://` filesystem
binding yet, and init performs no automatic remote fetch.

## Development use

Build with `make -j16 image`, then boot with networking explicitly enabled:

```sh
make run CPUS=4 VIRTIO_NET=1
```

In the guest:

```text
http-fetch http://example.com/ > home://example.html
cksum home://example.html
```

For known host files, run `python3 -m http.server 18080 --bind 127.0.0.1` in their
host directory, then fetch `http://10.0.2.2:18080/name` from QEMU user networking.
The consumer reports the fetch result, final HTTP/native/DNS status, body size,
reserved capacity and optional Content-Type to stderr. It writes the complete
body to stdout only after a successful fetch. Redirecting stdout can create or
truncate a local file even when the fetch later fails; failed fetching writes no
partial response into it. This program has one URI argument and no header,
credential, redirect, upload or retry options. It is a task-8 development consumer;
revisit its need when ordinary file opens work in task 9.

## Library and authority

`userspace/libhttp/http.h` declares `http_fetch`, `http_body_release` and the
result/storage structures. This is an application library, built as `libhttp.a`,
outside the SDK runtime. Its parser dependency is the pinned
[picohttpparser port](../ports/picohttpparser/README.md), exported separately at
`build/ports-dev/picohttpparser`. Consumers link both static libraries against the
same SDK. There are no kernel changes or new libc operations.

The caller supplies borrowed TCP, UDP, random and monotonic-clock capabilities
and a numeric DNS server. Numeric IPv4 destinations skip DNS and do not need
UDP/random authority. The library does no startup lookup or printing. The
consumer obtains its grants from startup and selects the existing `DNS_SERVER`
configuration. These grants permit access under existing networking policy;
this is not a destination sandbox.

The result distinguishes HTTP policy/parser failures, allocation/quota failures,
DNS answer failures and native network errors. The final status from a parsed
HTTP response remains available on later failure, with zero meaning no final
status. Native call status and DNS RCODE have separate fields. No errno mapping
or OPEN failure payload is introduced in this task.

## Request and response policy

One fetch sends one HTTP/1.1 GET with Host, Connection: close and
Accept-Encoding: identity. The request is framed by its header terminator;
TCP write shutdown waits until the response is complete. URI syntax accepts ASCII DNS names or numeric IPv4,
an optional decimal port, path and query. It rejects credentials, IPv6 literals,
raw characters outside URI syntax and malformed percent escapes. An empty path
becomes `/`; fragments are checked but never transmitted. Encoded path/query
bytes are preserved, without percent decoding or normalization.

Responses use HTTP/1.0 or HTTP/1.1. Status 200 returns bytes; 204 returns an empty
body without waiting for connection close. Other final statuses are retained and
rejected. Redirects are never followed, including HTTPS locations. There is no
TLS, decompression, connection reuse, cache or whole-request replay.

The client supports Content-Length, chunked and orderly-close framing. Fixed and
chunked bodies must finish completely. Close-delimited bodies accept orderly EOF;
there is no way to prove the origin intended to send no more bytes. Chunked bodies
finish at the terminal chunk and validated trailers, independently of peer EOF.
The pinned parser handles response/header syntax and chunk decoding; the library
validates framing policy and ignored chunk-extension syntax. Trailer fields are
parsed separately, not silently skipped.

The deliberately strict subset requires CRLF and rejects folded fields, duplicate
or comma-list Content-Length (even identical values), conflicting length/transfer
encoding, and transfer codings other than a single `chunked`. HTTP/1.0 transfer
encoding is rejected. Informational responses and 204 cannot carry body-framing
fields. Status 101 is unsupported. Content-Encoding must be absent or a single
`identity`. Duplicate Content-Type/Content-Encoding are rejected. Content-Type is
optional descriptive printable ASCII metadata, at most 127 bytes; excess is a
limit failure. Trailers cannot supply framing, content representation, Host,
Connection or Trailer fields.

## Bounds, storage and lifetime

| Resource | Limit |
| --- | --- |
| Complete URI | 2,048 bytes |
| Aggregate response headers and trailers, including informational responses | 32 KiB |
| Aggregate fields | 256 |
| Informational responses | 8 |
| Each chunk size/extension line | 8 KiB |
| Completed body | 16 MiB |
| Shared body-storage reservations | 64 MiB |
| Overall fetch time | 30 seconds, capped by an earlier caller deadline |

The unmodified parser also rejects excessive chunk framing overhead: once
accumulated overhead reaches 100 KiB, streams dominated by overhead can fail its
ratio bound. Tiny-chunk responses can therefore be rejected below the body limit.
Parser/request scratch is fixed and bounded separately from body storage; the
large receive buffer and field array live on the heap.

The deadline starts before URI processing and covers DNS, connect, send and the
complete response. Existing DNS attempts retain their three-second maximum and
at most two attempts, capped by this same absolute deadline. No retry,
informational response or successful short transfer refreshes it. CPU-side work
is checked before returning success as well.

The caller zero-initializes a `http_storage` account and keeps it alive until all
associated bodies are released. A successful result owns its bytes and capacity;
`http_body_release` frees them and returns the reservation. The account is for
single-task use. Copies of the C structure do not create additional ownership.
Release an existing successful result before reusing its output structure.

Fixed-length bodies reserve the declared size. Unknown lengths grow in bounded
powers of two, retaining the old allocation/reservation until the new allocation
exists and the bytes are copied. The 64 MiB ceiling counts that temporary overlap
as well as previously returned bodies. Allocation can fail below the ceiling;
there is no eviction. An empty body requires no body allocation. Every failed
fetch discards staging, returns its reservation and aborts/closes network state.
No body is exposed before success. Live snapshot/export counts and service
scheduling belong to task 9.
