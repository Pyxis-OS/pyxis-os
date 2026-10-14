# Capabilities and providers

Status: **design in progress, owner-led, from 2026-10-14.** This is the
capability-model track. It replaces four earlier documents: the capability-valued
environment (parked 2026-10-11), shell capability flow (#809), interchangeable
providers (2026-10-08) and userspace scheme providers (2026-09-24). Points marked
**decided** are owner direction, settled in discussion on 2026-10-14; nothing here is implemented or authorizes code.

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

## Typed capabilities (decided)

- **Opening returns a typed capability.** A client opens a target with an
  intent. The target issues a capability, and its kernel-attested type
  determines what the client can do with it:
  - a FILE: ordinary reads and writes;
  - a transaction: it accepts a body, commits and yields a reply;
  - later types (queues, row sets, sessions) bring their own rules.

  There is one open and no separate "what are you" exchange.
- **The issuer decides the rights.** A capability carries whatever its producer
  issued: `cat`, `echo` and `printf` hand out read-only output; a request
  preparer hands out a request that can be read, written and removed. Passing a
  capability on adds nothing.

## Universal operations (decided)

**Operations are the same for every capability.** Open, read, write and remove
mean the same thing whatever they act on; each capability defines what they do.
There is no separate endpoint mechanism.

- **One open.** `open(target, intent, deadline)` takes a target that is either a
  URI, resolved through the caller's namespace to a provider, or a capability
  the caller already holds. The intent is read, write or both, with file-style
  create, truncate and append, which a target may refuse. The result is
  whatever capability the target issues.
- **Names and held capabilities differ only in reach.** A URI is resolved
  through the caller's namespace binding, which may reach anything its provider
  serves. A held capability reaches only what it was issued for, so handing a
  program one prepared request delegates calling that one service.
- **Each target defines its operations:**

  | Target | Read | Write | Remove |
  |---|---|---|---|
  | FILE (`home://notes.txt`, or `$x` holding a writable file) | bytes | `>` truncates, `>>` appends | deletes |
  | HTTP URI (`https://…`) | GET | one-shot POST | DELETE |
  | Prepared request (`$api`) | GET | its prepared method: POST, PUT or PATCH | DELETE |
  | Read-only value (`$x` holding text or a reply) | its content | error | error |

  Each uses the same URL, headers and credentials. The client does the same
  thing in every case and dispatches only on what it gets back. `unset x`
  removes the variable; `rm $x` acts on what it holds.
- **Protocol options belong to preparers.** A preparer such as `http prepare`
  configures a request and issues a capability carrying it: request headers,
  including authorization such as a bearer token, the write method (POST, PUT
  or PATCH; DELETE is always remove) and the trust profile. The headers apply
  to every operation on that capability, read and remove included. Generic OPEN
  carries no protocol fields: today's HTTP origin, flags and budgets leave its
  request header.
- **Credential store: out of scope.** How a credential is retrieved from a
  credential store and handed to a preparer is deliberately left open (owner,
  2026-10-14); see [credentials](credentials-and-biometrics.md). This model
  only requires that a preparer can hold one without exposing it.
- **Redirects are transparent.** Providers follow them, including 307 and 308
  with the request body. Credentials are dropped when a redirect crosses to
  another origin, so a protected service that redirects elsewhere fails as
  unauthorized instead of leaking the token. The REDIRECT reply kind and its
  Location leave generic OPEN. One provider serves `http` and `https`; a
  redirect to a scheme it does not serve fails, with the Location in the
  failure detail.

The FILE-only path helpers stop refusing `http(s)` names: a program asks for an
intent, receives a typed capability, and handles that type or reports it as
unsupported.

## Transactions and commit (decided)

- A transaction accepts a body, commits, aborts and yields a reply. The reply is
  an ordinary readable capability.
- **Close commits, everywhere.** Closing a transaction commits it, whether the
  closer is the shell (after its producer succeeded), `fclose`, or exit. An
  explicit `fflush` commits earlier; `"w+"` then reads the reply from the same
  FILE. Implicit buffer drains only stage.
- **Abandoning fails loudly.** A transaction released without a commit (a
  failed producer, a crash, an explicit abort) reports a failure; it never
  turns silently into nothing.
- Editors that autosave do not hold transactions: a write that should not
  submit does not open one.
- No blind retries, and a lost reply never causes a mutation to run again. Other
  providers' mutations need their own commit, cancel, ownership and
  uncertain-outcome contracts.

## Failure (decided)

**A failure is part of the capability, and reading it behaves like reading a
file that does not exist.** The reading program reports the error at the source
and exits non-zero; nothing is turned into empty output.

| Outcome | Behaves like | Example |
|---|---|---|
| 404, 410 | missing file | `grep: https://bla: not found (404)` |
| 401, 403 | permission denied | `cat: https://bla: permission denied (403)` |
| 5xx, timeout, body cut off | I/O error, possibly mid-read | `cat: https://bla: I/O error (503)` |

Libc maps these outcomes to the matching `errno` values, so ports need no change.
Writes follow the same rule: a rejected submission fails like writing to a file
that refuses the write. A failed capability still carries its status and a
bounded error body, which failure-aware tools read through the facet API; plain
readers never see error bodies as data.

## Data flow in the shell

- **Writing to a target produces the target's response.** `>` sends a stage's
  output into the target, and the target's response becomes the stage's output.
  A FILE responds with nothing, as today; a transaction responds with its reply:

  ```sh
  printf '{}' > json+https://ci.example/jobs | jq .id
  ```

- **Pipes move capabilities (decided).** `A | B` hands A's output capability to
  B as stdin instead of copying bytes; the producer no longer holds it. Fan-out
  such as `tee` makes explicit, optionally narrowed copies. A byte stream stays
  the base type, so byte-only programs are unaffected, and large replies need no
  copy. A failed capability fails its reader as described above.
- **Command substitution binds a capability (decided).** `x=$(…)` binds the
  output capability of the command, not a string:

  ```sh
  api=$(http prepare PUT json+https://ci.example/jobs/42)
  job=$(printf '{}' > json+https://ci.example/jobs)
  id=$(cat $job | jq .id)
  ```

  As in POSIX, the assignment happens and the status reports any failure. If
  the failed capability passed through unchanged (`foo=$(cat https://bla)`),
  `foo` holds it and later reads of `$foo` fail the same way. If a program
  consumed it (`foo=$(grep foo https://bla)`), that program reported the error,
  and `foo` holds its output, exactly as with a missing file today.
- **Syntax.** Assignment is `name=value` with no spaces, and `$(…)` is command
  substitution. Variables are always referenced with `$`, including as
  redirection targets; a bare word is always a path. The shell has no
  assignment, expansion or substitution today, so all of this is new.

## Variables and the environment

- **Every variable holds a capability (decided).** There are no separate string
  and capability variables: `foo="true"` binds a read-only capability whose
  content is `true`.
- **Inheritance as today (decided).** `export foo=…` exports to children; a
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

"Endpoint" names a usage, not a separate kind: any capability whose write
starts a transaction, whether reached by URI or held as a prepared request
(decided). Held in a variable, it is reusable: every write starts a new,
independent transaction and yields its own reply.

```sh
api=$(http prepare POST json+https://ci.example/jobs)

printf '{}' > $api                          # submit, discard the reply
first=$(printf '{"ref":"main"}' > $api)
second=$(printf '{"ref":"next"}' > $api)

first_id=$(cat $first | jq .id)
cat $api                                    # GET on the same URL
rm $api                                     # DELETE on the same URL
```

- A prepared request is a configured resource (URL, request headers such as
  authorization, write method, deadline policy, backing), not a one-shot
  request. Writing
  never consumes it, and concurrent writes are separate transactions.
- Replies are independent of the endpoint: each has its own outcome, media type
  and lifetime.
- Holding it is the authority to call it. Private headers such as credentials
  live inside it, so the holder can call the service without being able to read
  the token.

## Libc

- **`fopen` dispatches on the capability type**, not the spelling;
  `descriptor_open()` stops classifying HTTP by name.
- **Commit follows the transaction rules above:** `fclose` and exit commit, an
  explicit `fflush` commits earlier, implicit drains only stage, and `"w+"`
  reads the reply. This replaces task 5c's "`fclose` only stages".
- **A small Pyxis facet API** (in `<pyxis/…>`, not stdio) lets a program ask what
  a stream is: generic outcome (succeeded or failed, with a provider-defined
  detail and bounded error body), media type (`application/json`, not
  `json+https`), length when known, and whether it can be re-read. It never
  reveals the provider, scheme or URI. Programs that ignore it read bytes.
- **Ports keep working.** Failures arrive as ordinary `errno` values; `getenv`
  and `setenv` read and write text-content variables as strings, and a non-text
  capability is not presented as a string. Expanded names such as `env://job`
  open like any other path.

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
8. **Provider policy:** caller isolation, credentials and cache reuse for any
   shared provider; asynchronous provider work and cancellation (see
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
