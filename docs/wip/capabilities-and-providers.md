# Capabilities and providers

Status: **design in progress, owner-led, from 2026-10-14.** This is the
capability-model track. It replaces four earlier documents: the capability-valued
environment (parked 2026-10-11), shell capability flow (#809), interchangeable
providers (2026-10-08) and userspace scheme providers (2026-09-24). Points marked
**decided** are owner direction; nothing here is implemented or authorizes code.

The owner's sequencing (2026-10-14): finish open work, resolve this model, then
continue. **No new libc, shell, web/HTTP or Git work starts until it is
resolved**; independent tracks such as drivers, storage and kernel work continue.

## Principles

1. **Resources are named by URIs and served by providers.** Parsing, TLS, SQL and
   Git stay in userspace providers; the kernel routes, carries IPC and enforces
   capabilities.
2. **A URI spelling only routes.** It selects a provider in the caller's
   namespace. What the caller can then do comes from the typed capability the
   provider issues, never from the text of the name.
3. **Programs depend on protocols, not on providers.** The launcher decides who
   serves each resource; a program cannot tell, and does not need to.
4. **Receivers stay provider-blind.** They see protocol facets, never the
   provider, scheme or URI that produced what they hold.
5. **Side effects are explicit.** Writing stages; a commit submits. There are no
   blind retries, and a lost reply never causes a mutation to run again.
6. **Representations are chosen, never guessed.** A prefix such as `json+`
   selects a representation; it does not change authority.
7. **Capabilities are not text.** A handle number is never printed or parsed back,
   strings confer no authority, and a restricted program cannot recover
   authority from strings.

## What exists today

- **Injected resources.** Programs receive named startup resources (`display`,
  `keyboard`, `pointer`, `clock`, `audio`, roots) from their launcher, as
  [processes](../interfaces/processes.md#startup-record) describe. Today these are
  kernel objects.
- **Userspace providers.** `text://`, `http://` and `https://`
  [file providers](../interfaces/file-providers.md) serve FILE capabilities;
  shared file helpers accept native and exported FILE handles alike.
- **Protocol identity.** Every handle has a kernel-attested protocol
  (`handle_query`), and [exported objects](../interfaces/userspace-services.md#interfaces-and-authority)
  carry object, protocol and rights metadata.
- **Prepared HTTP transactions** ([interface](../interfaces/http-transactions.md))
  return separate control, body and observation grants, and the shell's interim
  `=> name`, `@name` and `request` surface (task 5a) holds them in a shell-local
  table.
- **Environment variables are strings**, copied into each process at launch;
  [session configuration](../userland/session-configuration.md) already expects
  them to carry capabilities later.

## Finding: semantics from URI spelling (2026-10-14)

The shell and libc decide what a resource is by matching its URI text:

- the shell treats a redirection as an HTTP transaction when the target spells
  `http`, `https` or a `json+` alias of them (`http_uri()` in
  [shell/launch.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/shell/launch.c),
  via `provider_http_uri()` in
  [lib/provider_http.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/lib/provider_http.c)),
  and `=> name` after any other write refuses with
  `shell: => name requires a final HTTP URI stdout`;
- libc's `fopen` does the same (`descriptor_open()` in
  [libc/descriptor.c](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/libc/descriptor.c)).

That contradicts principles 2 and 3 in both directions: an alias or another
request/reply provider (an `api://` name for an HTTP service, an object store)
gets plain-file behaviour, and a namespace serving `https://` from something
else is still treated as HTTP. Each task took the narrowest correct path for its
own contract; the model was never checked against the founding intent.

## Typed capabilities

- **Opening returns a typed capability (decided).** A client opens a target with
  an intent, such as read or write. The provider issues a capability, and its
  kernel-attested type determines what the client does with it:
  - a FILE: ordinary reads and writes;
  - a transaction: it stages, commits and produces a reply;
  - later types (queues, row sets, sessions) bring their own rules.

  There is one open and no separate "what are you" exchange.
- **The issuer decides the rights (decided).** A capability carries whatever its
  producer issued: `cat`, `echo` and `printf` hand out read-only output; a
  request preparer hands out a request that can be staged and committed. Passing
  a capability on adds nothing.
- **Write-open follows (decided).** Opening a target for writing returns
  whatever its provider issues. Transaction semantics come from a provider or
  preparer that issues a transaction, not from a client-chosen PREPARE path.

## Universal operations (decided 2026-10-14)

**Operations are the same for every capability.** Read, write and open mean the
same thing whatever is being read, written or opened; each capability defines
what its operations do. There is no separate endpoint mechanism.

- **One open.** `open(target, intent, deadline)` takes a target that is either a
  URI, resolved through the caller's namespace to a provider, or a capability
  the caller already holds. The intent is read, write or both, with file-style
  create, truncate and append, which a target may refuse. The result is
  whatever capability the target issues, typed by the kernel.
- **Each target defines its default write.** Writing to `https://…` performs
  the HTTP provider's default write, a one-shot POST. Writing to a prepared
  request performs the write that request was prepared with: POST, PUT, PATCH
  or DELETE. Writing to a FILE stores bytes. The client does the same thing in
  every case and dispatches only on what it gets back.
- **Protocol options belong to preparers.** Methods, extra headers,
  credentials and trust profiles come from a preparer such as `http prepare`,
  which issues a capability carrying them. Generic OPEN carries no protocol
  fields; today's HTTP origin, flags and budgets leave its request header.
- **Providers follow redirects** under their own policy. The REDIRECT reply kind
  and its Location leave generic OPEN. One provider serves `http` and `https`;
  a redirect to a scheme it does not serve fails honestly, with the Location in
  the outcome detail.
- **A transaction** accepts a body, commits, aborts and yields a reply. The
  reply is an ordinary readable capability with facets, so any reader can use
  it. What triggers the commit is client policy keyed on the type: the producer
  closing its output in the shell, `fflush` in C.
- **Names and held capabilities differ only in reach.** A URI is resolved
  through the caller's namespace binding, which may reach any URL that provider
  serves. A held capability reaches only what it was issued for, so handing a
  program one prepared request delegates calling that one service.

The FILE-only path helpers stop refusing `http(s)` names: a program asks for an
intent, receives a typed capability, and handles that type or reports it as
unsupported.

## Writes and transactions

The owner's 2026-09-24 direction for HTTP still holds, expressed through types:
reading a URI is GET; a write stages a body and an explicit commit submits it;
`"w+"` reads the reply after the commit. Ordinary writes, flush on close, exit
and editor autosave never submit. PUT, PATCH and DELETE are writes to a prepared
request; other providers' mutations need their own commit, cancel, ownership and
uncertain-outcome contracts. A newly opened URI is read with GET, never by replaying an earlier
response.

## Data flow in the shell

- **Writing to a target produces the target's response.** `>` sends a stage's
  output into the target, and the target's response becomes the stage's output.
  A FILE responds with nothing, as today; a transaction responds with its reply,
  committing when the producer closes its output:

  ```sh
  printf '{}' > json+https://ci.example/jobs | jq .id
  ```

- **Blocks bind their output as a capability.** `x = { … }` binds a block's
  output, not a string. Several transactional writes in one block reply in
  order; bind each one to keep them separate.

  ```sh
  job = { printf '{}' > json+https://ci.example/jobs }
  files = { ls home:// }
  cat $job
  ```

- **Pipes move capabilities (decided).** `A | B` hands A's output capability to
  B as stdin instead of copying bytes; the producer no longer holds it. Fan-out
  such as `tee` makes explicit, optionally narrowed copies. A byte stream stays
  the base type, so byte-only programs are unaffected, while a reply arrives
  with its facets (reader, outcome, media type) and large replies need no copy.
  Outcome travels with the data, so no pipe rule has to fail a stage on an
  HTTP error; the consumer or the shell decides.
- **Syntax.** Braces belong only to assignment; a plain command such as
  `printf '{}' > $endpoint` needs none. Variables are always referenced with
  `$`, including as redirection targets; a bare word is always a path.

## Variables and the environment

- **Every variable holds a capability (decided).** There are no separate string
  and capability variables: `foo = "true"` binds a read-only capability whose
  content is `true`.
- **Inheritance as today (decided).** `export foo = …` exports to children; a
  plain assignment stays local. Exporting a writable capability delegates its
  write authority, and that explicit export is the control. Inheritance can
  narrow rights, never widen them.
- **Lifetime (decided).** Overwriting or unsetting a variable releases its
  capability; shell and job exit follow the existing cleanup rules.
- **Retention (decided).** Bound values and re-readable replies use a bounded
  backing policy like today's private RAM staging, with explicit opt-in for
  large values.
- **Expansion.** `cat $job` passes the child a name it can open, such as
  `env://job`, plus a grant of that entry, so every program that opens files can
  use it. The interim `@name` table goes away.
- **Cheap text values.** A text value only has to behave as a read-only
  capability; it can live as an immutable inline value (for example in the
  startup record) rather than as a kernel object per variable.
- **Secrets.** A capability to use a secret is not the secret; credential bytes
  stay out of argv, environment text, history and logs.
- Configuration documents (session and space Lua) set variables too, so a user
  directory authorization or a credential-service grant can travel through a
  session.

## Endpoints

"Endpoint" is a name for usage, not a separate kind: any capability whose write
starts a transaction, whether reached by URI or held as a prepared request
(decided). Bound to a variable, it is reusable: every write starts a new,
independent transaction and yields its own reply.

```sh
endpoint = { http prepare POST json+https://ci.example/jobs }

printf '{}' > $endpoint                     # submit, discard the reply
first = { printf '{"ref":"main"}' > $endpoint }
second = { printf '{"ref":"next"}' > $endpoint }

first_id = { cat $first | jq .id }
```

- It is a template (method, URI, headers, deadline policy, backing), not a
  one-shot request. Writing never consumes it, each write is an explicit new
  submission, and concurrent writes are separate transactions.
- Replies are independent of the endpoint: each has its own outcome, media type
  and lifetime.
- Holding an endpoint is the authority to call it. Private headers such as
  credentials can live inside it, so the holder can call the service without
  being able to read the token.
- A derived value such as `first_id` is plain read-only content; the outcome
  facet stays on `$first`.

## Libc

- **`fopen` dispatches on the capability type**, not the spelling;
  `descriptor_open()` stops classifying HTTP by name.
- **`fflush(f)` commits because the FILE is transactional.** The task 5c rules
  carry over, keyed on type: an explicit flush commits once, implicit drains,
  `fclose` and exit only stage, and `"w+"` reads the reply from the same FILE.
- **A small Pyxis facet API** (in `<pyxis/…>`, not stdio) lets a program ask what
  a stream is: generic outcome (succeeded or failed, with an optional
  provider-defined detail for display), media type (`application/json`, not
  `json+https`), length when known, and whether it can be re-read. It never
  reveals the provider, scheme or URI. Programs that ignore it read bytes.
- **Ports keep working.** `getenv` and `setenv` read and write text-content
  variables as strings; a non-text capability is not presented as a string.
  Expanded names such as `env://job` open like any other path.

## Interchangeable providers

The launcher chooses who serves each protocol: dependency injection in OS form.
Plan 9's rio serves each window the same draw, mouse and keyboard files the
kernel serves rio; Genode parents route session requests to a driver or to a
GUI server implementing the same sessions. Pyxis already does this for FILE.
Extending it to display, input and other device protocols (first for a
[compositor](desktop-graphics.md#where-to-start-on-a-compositor)) needs:

1. **Memory shared between processes**, so a compositor sees client pixels
   without copying each frame.
2. **Provider-backed device handles:** `display_submit`, `pointer_read`,
   `keyboard_read` and `wait_many` reach a provider when the handle is exported,
   as the file helpers do.
3. **One protocol definition per interface**, satisfied by kernel and userspace
   providers alike, extended in place rather than versioned.
4. **Guarantees that stay in the kernel:** Super+Esc and space switching keep
   working with a provider in between.
5. **Who may act as a provider:** a compositor sees and can forge everything its
   clients see and type, so becoming one is a launch-time grant decision.

Other uses: nested compositors, scripted input and captured displays for
testing, remote display and input, and audio routed through a mixer.

## Representations

The resource (identity, lifetime, operations, available representations) is
separate from the caller's grant. `json+http` and `json+https` select JSON
Accept and Content-Type; they do not change the method, validate JSON or add
authority. Exact-name scoped bindings remain the kernel boundary, not a kernel
parser for composable prefixes. A later interface may let consumers discover and
select representations; selecting a format must never broaden access, run a
mutation or require guessing from a filename.

## SQLite views and query results

A second provider, after a native SQLite port. Start with read-only views or
published queries readable as files:

```sh
ls sqlite://catalog/
cat json+sqlite://catalog/apps | jq '.[].name'
```

The provider receives database authority through capabilities and checks which
views a caller may read; a database name grants nothing. Each result is a
bounded, fully materialized, read-only snapshot that stays stable until its last
reference closes. Time, row, byte and storage budgets come before
implementation, a truncated result is never published as complete, and the
first slice can use fixed views before admitting caller-supplied SQL.

## Later database sessions

An explicit session resource can accept structured requests: statements,
parameters, fetches, commit and rollback. A textual marker in a byte stream does
not define request boundaries. Uncommitted work rolls back when the session is
destroyed, not when one copied handle closes, and a lost reply never prompts
re-execution of a mutation.

## Database worksheet experiment

An editor could open `sqlite://catalog/worksheets/scratch`, type a `SELECT`,
save, and see the result. Query, result and error stay separate state so output
never destroys the query. Saving does not make an editor reread a file, so this
needs distinct query and result resources or an editor hook, not autoread alone.
The first experiment is read-only; autosave must never commit a database change.

## Git scheme

Lowest priority. `git://pyxis` could name a repository registered in the
caller's namespace, backed by a local repository grant: `ls git://pyxis/branches`,
`cat git://pyxis/commits/main/tree/src/main.c`, `json+git://…` with jq. A branch
resolves to a commit when a resource opens and stays fixed for consistent
reads. Names grant no repository authority. It must not drive current
interfaces.

## Response cache

A later userspace cache may keep complete, validated responses under a byte
budget, evicting by LRU and obeying HTTP caching rules (`Cache-Control`,
validators, `Vary`, authenticated responses; RFC 9111). A local time limit never
authorizes forbidden reuse, namespace and credentials define the sharing
boundary, and eviction never invalidates storage still held by open handles.
Partial bodies never satisfy another open.

## Open questions

1. **The facet API.** Its exact facets, errors and how it reaches Lua.
2. **Configuration documents** that name a capability they don't hold: who
   resolves the name, with what authority.
3. **Revocation:** how a capability is revoked and what a holder sees.
4. **Launch grants and namespaces:** whether capability variables, launch grants
   and namespace bindings are one mechanism or three that overlap.
5. **Retention numbers:** default and maximum backing for bound values.
6. **Providers:** how a provider learns its client exited and a client sees its
   provider gone; whether a client may hold both a provider's and the kernel's
   handle for one protocol; and the cost of interposition, measured against
   direct kernel delivery before committing a protocol to it.
7. **Shared memory:** the capability type, mapping and lifetime rules.
8. **Provider policy:** caller isolation, credentials, redirects and cache reuse
   for any shared provider; asynchronous provider work and cancellation (see
   [HTTP responsiveness debt](../technical-debt.md#http-provider-responsiveness)).

## Migration from today's interim

- Task 5a (`=> name`, `@name`, `request`) stays as shipped until the model lands,
  and is then replaced without keeping the old forms.
- HTTP tasks 5b and 5c are parked (2026-10-14); their branches are kept.
- New code adds no URI-spelling checks. `http_uri()`, `provider_http_uri()` and
  the `descriptor_open()` classification are the first things to replace.
- [Nested execution groups](nested-execution-groups.md) and declared bundle
  grants remain the authority model: passing a capability through a pipe or a
  variable never adds launch or control authority.
- Related directions: [users and authority](users-and-authority.md),
  [credentials](credentials-and-biometrics.md),
  [control, events and faults](control-events-faults.md), [spaces](spaces.md),
  [filesystems and namespaces](vfs.md) and
  [subsystems per space](subsystems-per-space.md).
