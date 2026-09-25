# Shell streams and capability handoff

Status: selected next direction. These notes record the agreed mechanism;
the byte-I/O contract and focused implementation tasks still need discussion.
No implementation is assigned by this document.

## Pipes and redirection

Pipes and redirection delegate actual capabilities through child startup
bindings. They do not pass numeric handle values as arguments or environment
strings: a handle identifies a grant in one process's table, not a globally
usable resource identifier.

| Shell operation | Capability handoff |
| --- | --- |
| `producer \| consumer` | Create a bounded byte stream; give the producer its writable endpoint as stdout and the consumer its readable endpoint as stdin. |
| `command > file` | Open/create the destination under the shell's authority and give the child a writable resource as stdout. |
| `command < file` | Open the source and give the child a readable resource as stdin. |
| Prepared provider request | Give a producer the request-body writer or a consumer the response reader through the same bindings. |

Libc maps stdin/stdout/stderr onto the granted resources. Programs using those
streams need not know whether bytes come from a terminal, file, pipe or provider.
The shell should delegate only the rights each child needs and handle stderr
explicitly; its exact redirection syntax remains to be designed.

Copied grants are sufficient for initial child setup. After successful handoff,
the shell closes temporary copies it no longer needs. Capability moves are not
a prerequisite for pipelines or redirection. Closing the shell's copy does not
revoke the child's copy; the resource remains alive through retained references.

Reference lifetime is part of stream behavior. Once queued bytes are drained,
the reader must be able to observe EOF when the last writer closes. An unused
writer retained by the shell or another child would prevent that. Reader closure
must also reach blocked writers through a defined error/wakeup path. Specify
partial-launch failure and process-exit cleanup alongside the successful case.

## Common byte-I/O contract

Define the common read/write behavior without erasing resource differences:
files may support seeking, pipes have sequential buffered data, and a staged
provider request has explicit submission and response phases. Short I/O, EOF,
blocking, queue capacity, backpressure and errors need concrete rules. Ordinary
buffer flushing or closing one grant must not accidentally commit a request.

Concurrent pipeline launch, child exit observation and cleanup need a bounded
first contract; this does not require full job control or POSIX process semantics.
Use existing launch-time delegation where it fits. Returning a newly prepared
resource from a running helper to the shell is a separate handoff to design,
as described in the [HTTP helper proposal](userspace-scheme-providers.md#prepared-requests-and-shell-handoff).

Shell variables that hold capabilities could eventually make prepared resources
convenient to use across commands. This is a possible language feature, not a
selected syntax or a requirement for the first pipelines milestone. HTTP helpers,
scheme providers and database sessions remain later consumers of this mechanism.
