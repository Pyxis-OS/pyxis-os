# Capability flow in the shell

Status: **proposal from an owner discussion, 2026-10-14.** Nothing here is
accepted, assigned or authorized as code. It belongs with the parked
[capability-valued environment](capability-environment.md) and should be decided
together with it, as one unit. The HTTP task 5 `=> name`, `@name` and `request`
surface is the shipped interim; this proposal would replace it, without keeping
the old forms for compatibility.

## Finding: semantics come from URI spelling

Clients decide what a resource is by matching the text of its URI, not by what
they were given:

- The shell treats a redirection as an HTTP transaction when its target spells
  `http`, `https` or a `json+` alias of them
  (`http_uri()` in [shell/launch.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/shell/launch.c), via
  `provider_http_uri()` in [lib/provider_http.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/lib/provider_http.c)).
  `=> name` after a write to any other target refuses with
  `shell: => name requires a final HTTP URI stdout`.
- Libc's `fopen` makes the same decision
  (`descriptor_open()` in [libc/descriptor.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/libc/descriptor.c)), so the
  plain-C POST work (task 5c) inherits it: `fflush` commits only for HTTP
  spellings.

The [interchangeable providers](interchangeable-providers.md) direction says
programs depend on protocols and launchers choose providers. Spelling-based
dispatch contradicts it in both directions:

- an alias or another provider with request/reply semantics (an `api://` name
  for an HTTP provider, a future `git+https` or object store) gets plain-file
  behaviour;
- a namespace that serves `https://` from something else is still treated as
  HTTP.

## Direction

**Capabilities carry semantics; URI spellings only route to a provider.**

1. **Opening returns a typed capability.** A client opens a target with an
   intent, such as write. The provider returns a capability, and its type is
   kernel-attested (every handle has a protocol, reported by `handle_query`);
   neither the URI text nor a provider's claim decides it. Clients dispatch on
   the type of what they hold:
   - a FILE: an ordinary write;
   - a transaction (today's PREPARE facets: control, body and observation): it
     stages, commits, and produces a reply;
   - later types bring their own rules, with no client-side guessing.

   One open, no separate "what are you" exchange. The design work is a generic
   write-open intent; today HTTP uses a separate PREPARE path that the client
   picks because of the scheme name.

2. **Writing to a target produces the target's response.** `>` sends a stage's
   output into the target, and the target's response becomes the stage's
   output. A FILE responds with nothing, which is today's behaviour; a
   transaction responds with its reply:

   ```sh
   printf '{}' > json+https://ci.example/jobs | jq .id
   ```

   The transaction commits when the producer closes its output, the same commit
   point as today.

3. **Blocks bind their output as a capability.** `x = { … }` binds a block's
   output as a capability, not a string. Several transactional writes in one
   block reply in order; anyone who wants them separately binds each one.

   ```sh
   job = { printf '{}' > json+https://ci.example/jobs }
   cat $job
   files = { ls home:// }
   ```

   A bound value is retained so that it can be read more than once, under a
   bounded backing policy like today's private RAM staging, with explicit
   opt-in for large values. `=>` becomes unnecessary, or at most sugar.

4. **Pipes pass capabilities.** Startup stdin and stdout are already
   capabilities. `A | B` hands A's output capability to B as stdin instead of
   copying bytes through a pipe:
   - a byte stream stays the base type, so programs that only read bytes are
     unaffected;
   - a reply arrives with its facets: reader, status (HTTP code, framing) and
     content type. A capability-aware consumer can ask what its stdin is, and
     large replies need no copy;
   - status travels with the data, so no rule has to make a non-2xx response
     fail the stage; the consumer or the shell decides;
   - content types reaching consumers and terminals are where
     representations come in, per the project's founding idea.

## Libc

The same model applies to C programs, through ordinary stdio:

- **`fopen` dispatches on the capability type** returned by the generic
  write-open (or read-open), not on the URI spelling. `descriptor_open()` stops
  classifying HTTP by name; a provider that returns a transaction gets
  transaction behaviour whatever its scheme.
- **`fflush(f)` commits because the FILE is transactional.** Task 5c's rules
  (an explicit flush commits once; implicit drains, `fclose` and exit only stage;
  `"w+"` reads the reply from the same FILE) stay, keyed on the FILE's type.
- **A FILE can expose its capability's facets.** A small Pyxis API (in
  `<pyxis/…>`, not stdio) lets a program ask what a stream is: its type, status
  (for example an HTTP code) and content type. This is how a C program reads
  the status and content type of a reply handed to it as stdin by a
  capability-passing pipe. Programs that ignore it keep reading bytes.
- **Receivers stay provider-blind.** Every readable capability offers the same
  byte reader, so a program that only reads cannot tell an HTTP reply, a file, a
  SQLite result or a Git object apart, and does not need to. The facet API
  exposes **protocol facets only**: generic outcome (succeeded or failed, with an
  optional provider-defined detail for display), media type (`application/json`
  rather than `json+https`), length when known, and whether it can be re-read.
  It never reveals the provider, scheme or URI. Branching on the source would
  recreate the spelling problem one layer down;
  [interchangeable providers](interchangeable-providers.md) depend on it.
- **`$job`-style expansion needs nothing new in libc**: the child opens the
  expanded name (such as `env://job`) like any other path, through its grant.

## Variables

With the [capability environment](capability-environment.md), `$job` holds a
typed capability. Expanding it as an argument (`cat $job`) yields a name the
child can open, such as `env://job`, plus a grant of that entry. Every program
that opens files can then use it, not only shell redirections. The separate
`@name` table goes away.

## Owner answers to the open questions (2026-10-14)

Owner direction, still part of this parked proposal; nothing is implemented.

1. **Every variable holds a capability.** There are no separate string and
   capability variables: `foo = "true"` binds a capability whose content is the
   text `true`. No ambiguity, no type check before use. Variables reach children
   exactly as variables do today: `export foo = "true"` exports, a plain
   assignment stays local. Exporting a writable capability therefore delegates
   that write authority to children; that's the explicit control.
2. **The issuer decides the rights.** A capability carries whatever its producer
   issued: `cat`, `echo` and `printf` hand out read-only output; a request
   preparer (today's `request prepare`, perhaps an `http prepare` program) hands
   out a read-write request that can be staged and committed. Pipes and
   expansion pass on what was issued and add nothing.
3. **A pipe moves the capability**; the producer no longer holds it. Fan-out such
   as `tee` makes explicit copies, narrowed where wanted.
4. **Lifetime:** overwriting or unsetting a variable releases its capability;
   shell and job exit follow the existing cleanup rules.
5. **Retention:** bound values and re-readable replies use a bounded backing
   policy like today's private RAM staging, with explicit opt-in for large
   values.
6. **Write-open follows from 2:** opening a target returns whatever capability its
   provider issues. Transaction semantics come from a provider or preparer that
   issues a transaction, not from a separate client-chosen PREPARE path.

## Reusable prepared requests

From the owner discussion, 2026-10-14. A prepared request bound to a variable is
an **endpoint**: every write to it starts a new, independent transaction and
yields its own reply.

```sh
endpoint = { http prepare POST json+https://ci.example/jobs }

printf '{}' > $endpoint                     # a plain command: submit, discard the reply
first = { printf '{"ref":"main"}' > $endpoint }
second = { printf '{"ref":"next"}' > $endpoint }

first_id = { cat $first | jq .id }          # values flow like any other capability
```

- **The endpoint is a template, not a one-shot request.** It holds the method,
  URI, headers, deadline policy and backing choice. Writing to it never consumes
  it; each write is an explicit new submission (no hidden retries), and
  concurrent writes are separate transactions.
- **Replies are independent of the endpoint.** Each reply has its own outcome,
  media type and lifetime. Unsetting the endpoint does not affect replies
  already bound, and vice versa.
- **Holding an endpoint is the authority to call it.** Private request headers
  such as credentials can live inside the endpoint capability: the holder can
  make requests without being able to read the token. Passing an endpoint
  delegates calling, not the secret.
- **Deriving values:** `first_id = { cat $first | jq .id }` binds new read-only
  content. Like any text value it is a capability; it no longer carries the
  reply's outcome facet, which stays on `$first`.
- Syntax: braces belong only to assignment (`x = { … }`); a plain command such as
  `printf '{}' > $endpoint` needs none. A variable is always
  referenced with `$`, including as a redirection target (`> $endpoint`); a bare
  word stays an ordinary path.

## Implementation notes

For when this is picked up:

- A text value only needs to behave as a read-only capability. It may be held as
  an immutable inline value (for example in the startup record) rather than as a
  separate kernel object per variable, so large environments stay cheap.
- Ports keep working: libc `getenv`/`setenv` read and write text-content
  capabilities as strings; a non-text capability is not presented as a string.

## Relation to current work

- Task 5a (`=> name`, `@name`, `request`) and 5c (`fflush` commit) stay as
  shipped. New code should not add further spelling checks.
- [Nested execution groups](nested-execution-groups.md) and declared bundle
  grants remain the authority model: passing a capability through a pipe or
  variable never adds launch or control authority.
- Revisit together with the capability environment, before session or space
  configuration stores capabilities, and before more programs depend on
  `provider_http_uri`.
