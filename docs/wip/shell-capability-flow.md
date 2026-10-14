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

## Open questions

1. **Inheritance.** Do capability variables reach children automatically or only
   when exported? The rule must hold for any capability, not only read-only
   replies.
2. **Facets passed downstream.** The proposed default for pipes and expansion is
   read-only (reader and status), never control or commit, unless granted
   explicitly.
3. **Move or copy.** A pipe moves the capability, so the producer no longer holds
   it. Fan-out such as `tee` makes explicit, narrowed copies.
4. **Lifetime.** Overwriting or unsetting a variable releases its capability.
   Shell and job exit follow the existing cleanup rules.
5. **Retention bounds.** Backing, limits and failure behaviour for `x = { … }`
   and for re-readable replies.
6. **Generic write-open.** The intent and request shape providers implement, and
   how PREPARE becomes one case of it.

## Relation to current work

- Task 5a (`=> name`, `@name`, `request`) and 5c (`fflush` commit) stay as
  shipped. New code should not add further spelling checks.
- [Nested execution groups](nested-execution-groups.md) and declared bundle
  grants remain the authority model: passing a capability through a pipe or
  variable never adds launch or control authority.
- Revisit together with the capability environment, before session or space
  configuration stores capabilities, and before more programs depend on
  `provider_http_uri`.
