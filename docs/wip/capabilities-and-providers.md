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
2. **A URI spelling only routes.** It selects an entry in the caller's
   environment. What the caller can then do comes from the typed capability the
   provider issues, never from the text of the name.
3. **Programs depend on protocols, not on providers.** The launcher decides who
   serves each resource; a program cannot tell, and does not need to.
4. **Providers are transparent.** A program cannot tell which provider served
   what it holds: it sees protocol facets, never the provider, scheme or URI.
   Protocol knowledge lives in three places only: the provider itself, the
   preparer that configures requests for it, and human-readable failure detail.
   Every other decision in this document follows from that.
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
gets plain-file behaviour, and an environment whose `https` entry is served by
something else is still treated as HTTP. Each task took the narrowest correct path for its
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
  URI, resolved through the caller's environment to a provider, or a capability
  the caller already holds. The intent is read, write or both, with file-style
  create, truncate and append, which a target may refuse. The result is
  whatever capability the target issues.
- **Names and held capabilities differ only in reach.** A URI is resolved
  through the caller's environment entry, which may reach anything its provider
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
  removes the variable; `rm $x` acts on what it names. Removing through a held
  FILE capability without a name is refused for now.
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
| over a retention limit | file too large | `x: too large to retain (limit 64 MiB)` |

Libc maps these outcomes to the matching `errno` values, so ports need no change.
Writes follow the same rule: a rejected submission fails like writing to a file
that refuses the write. A failed capability still carries its status and a
bounded error body, which failure-aware tools read through `describe` (see Facets); plain
readers never see error bodies as data.

## Facets (decided)

**`describe` is a universal operation on any readable capability.** It is
answered by the capability's issuer: the kernel for native files, the provider
for exported ones. It does not block; a read open has already waited for its
reply to begin.

| Facet | Meaning | Example |
|---|---|---|
| type, rights | kernel-attested, not the issuer's claim | FILE, read |
| outcome | ok, not found, denied, I/O error, too large, aborted or uncertain | not found |
| detail | text for people only; programs never parse it | `404 Not Found` |
| error body | bounded, handed out as its own readable capability | an API's JSON error |
| media type | declared by the producer, never guessed | `application/json` |
| length | when known | 5120 |
| re-readable | a snapshot rather than a one-pass stream | yes |
| complete | fully arrived, or still arriving | yes |

- **Programs branch only on the outcome class.** Protocol numbers such as 404
  live in the detail text, so an HTTP 404 and a SQLite "no such view" both
  arrive as *not found*.
- **No URI, scheme or provider identity.** Today's METADATA reply, which returns
  the endpoint URI, goes away. An error message names the target because its
  reader knows what it opened: `grep: https://bla: not found (404)`.
- **Uncertain** means a write may or may not have taken effect, as when its
  reply was lost (today's submission certainty: not started, attempted, sent).
  Nothing retries it automatically. Libc reports it as an I/O error.
- **Response headers are not facets.** A caller that needs them (ETag,
  pagination links, rate limits) asks its preparer for a reply representation
  that includes them, such as a JSON envelope with status, headers and body,
  read with ordinary tools.
- **Producers declare the media type of their output**, and only that: `jq`
  marks its output `application/json`, and the type travels through pipes to
  whatever reads it, later including a terminal that renders it. Nothing infers
  a type from content or names.
- **Facets declare; they do not prove.** A provider can mislabel content, as
  OPEN metadata can today. Length and completeness on a live stream describe its
  current state, not a promise.

Surfaces: `pyxis_describe(FILE *, struct pyxis_facets *)` in `<pyxis/…>`, with a
call that returns the error body as a `FILE *`; `pyxis.describe(f)` in Lua,
returning a table; and a native `describe` command in the shell, chosen over
`stat`, whose familiar meaning differs.

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

## One environment (decided)

**Every name a process has is an entry in one table, its environment, and every
entry holds a capability.** Today six channels carry names into a process:
standard streams, named startup resources, scheme roots, the namespace grant,
bundle grants and string environment variables, each with its own rules and
checks against the others. They become one.

| Entry | Holds | Used as |
|---|---|---|
| `stdin`, `stdout`, `stderr` | streams, pipes, files | libc descriptors 0, 1 and 2 |
| `home`, `boot`, `tmp`, `bin` | directories | `home://notes.txt` |
| `https`, `text`, `json+https` | service slots | `https://ci.example/jobs` |
| `display`, `clock`, `audio` | devices | resolved by libpyxis |
| `TERM`, `job`, `api` | values, replies, prepared requests | `$TERM`, `$job` |

- **`name://rest` and `$name` refer to the same entry.** `name://rest` looks up
  `name` and opens `rest` on it, so the spelling only routes. `cat $job` passes
  the child `job://` together with that entry. The interim `@name` table and the
  earlier `env://` idea go away.
- **Children receive exactly what is exported.** Launch passes the exported
  entries and nothing else, which is the grant list; there is no map that
  exposes every name to whoever can look one up.
- **Services sit behind re-targetable slots.** Starting a service outputs a slot
  capability: `https=$(service start httpfs)`. The slot's manager (the
  supervisor or session that started it) can point it at a restarted provider,
  and running programs reach the new one on their next open, as a stable socket
  path does on Linux. Pointing a slot at nothing makes later opens fail like a
  missing file. Kernel whole-map namespaces retire in favour of these slots.
- **Shadowing is allowed.** `https=$(service start mock-http); export https;
  ./client` runs a program against a different provider, unchanged. That is
  interchangeable providers in shell form; like overriding `PATH`, it is local
  unless exported. No names are protected.

### Inversion of control

The model is dependency injection in operating-system form: the environment is
the container's scope, the launcher (shell, session or supervisor) is the
container, and a bundle manifest declares the program's dependencies like a
constructor signature. A program resolves what it needs by name and never by
ambient default. When libc starts, it asks for whatever backs `stdin`, `stdout`
and `stderr`; whether the shell bound a terminal, a pipe,
`2> home://err.log` or a copy of `stdout` for `2>&1`, libc cannot tell. The
same holds for `clock`, `display` or `https`.

**Absent dependencies:**

- A **required** bundle dependency that cannot be satisfied fails at launch,
  with a message naming the missing entry.
- An **optional** dependency may be absent, for example a microphone or camera
  the user did not grant, or a device that does not exist. The program checks
  for it and decides what to do, such as disabling a feature. Resolving it
  reports *not found* (no such device) or *denied* (not granted), using the
  facet outcome classes.
- Anything else, including an unbound stream or a name a plain program looks
  up, is valid until used and then fails like a missing file, as an absent
  stream does today.

Consequences for the remaining questions:

- A **configuration document** names capabilities by resolving entries in its
  own environment, so it can hand out only what it holds.
- **Revocation** uses forwarders, of which service slots are one; see
  Revocation below.

Costs: the startup record (64 KiB budget today) and the 64-binding namespace
limit become one general table whose limits need revisiting, and every URI open
does one table lookup first.

## Revocation (decided)

Today a provider can withdraw an export, which stops every copy of it at once,
and kernel objects cannot be revoked at all: closing a granting handle leaves
copies already granted intact. Revocation uses one mechanism instead, the
**forwarder** (a caretaker, in capability-system terms). The service slot above
is one.

- **A forwarder stands in for a capability and has two management rights:**
  *retarget*, to another capability of the same type, and *cut*. A service slot
  has both, so a supervisor can restart a provider. A **revocable copy** has
  only *cut*: retargeting a file that its holder believes is `notes.txt` would
  be a trap.
- **The forwarder's creator keeps the management handle.** That is the
  delegator, not necessarily the issuer, so whoever handed access out can take
  it back without asking the provider.
- **Plain copies stay irrevocable** and cost nothing extra, as today. A
  delegator asks for a revocable copy explicitly. Launchers use revocable copies
  wherever access may need taking back: consent-based device grants such as a
  microphone or camera always, so a user's privacy switch can withdraw them
  mid-run, and everything a session or space hands out, so teardown revokes it.
- **No escape by copying.** Cutting a forwarder also cuts every copy derived
  through it. The kernel tracks derivation only for revocable copies, so the
  cost stays where revocation is used.
- **The holder sees *denied*** (the facet outcome class; `EACCES` in libc).
  Queued operations fail; operations already delivered to a provider complete,
  as with export withdrawal today; later opens of the entry fail the same way.
  Data already read stays read.

```sh
mic=$(revocable $microphone)
export mic; ./recorder &
revoke ${mic.control}            # the recorder's next read is denied
```

Owner accepted this shell spelling on 2026-10-15: `revocable` publishes the holder
and private `mic.control` atomically. Copying or exporting `mic` does not delegate
its controller; `revoke` requires the explicit CUT controller. Revoking a
shared-memory mapping also unmaps it under the accepted
[shared-memory milestone](shared-memory.md#2-strong-explicit-cut-and-owned-automatic-withdrawal);
implementation is in progress.

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
- **Retention (decided).** Only one-pass output is copied. When `x=$(…)`
  captures something already re-readable (a file, an HTTP GET snapshot, a
  stored reply), the variable holds that capability with no copy and no limit:
  `x=$(cat tmp://big.json)` costs nothing. Output produced on the fly, such as
  `x=$(jq …)`, `x=$(ls home://)` or a streamed reply, is retained in RAM so it
  can be read again:
  - **Limits:** 64 MiB per retained value and 512 MiB per session, charged to
    the session's space and set in session configuration.
  - **Over a limit, nothing is truncated.** The variable holds a failed
    capability whose outcome is *too large* (`EFBIG` in libc), the status is
    non-zero, and the message suggests a file:
    `x: too large to retain (limit 64 MiB); write it to a file instead`.
    Writing the data to a file and binding the file is the opt-in for large
    values; there is no other mechanism.
  - **Sharing:** exporting or copying a retained value shares its backing.
    It stays charged to whoever retained it, not to the readers, and is
    released when the last holder lets go.
- **Expansion (decided).** Text values from literal assignments expand to their
  text, as in bash: `f="home://notes.txt"; echo hi > $f` writes to
  `home://notes.txt`, resolved through the caller's own environment. Every
  other value passes as `x://` with its entry, so `cat $job` reads the reply
  and `echo $job` prints `job://`. Inline `$(…)` used as an argument passes a
  capability, as bash's `<(…)` does.
- **Cheap text values.** A text value only has to behave as a read-only
  capability; it can live as an immutable inline value (for example in the
  startup record) rather than as a kernel object per variable.
- **Secrets.** A capability to use a secret is not the secret; credential bytes
  stay out of argv, environment text, history and logs.
- Configuration documents (session and space Lua) set entries too, so a user
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
- **Facets** come through `pyxis_describe()`, described below. Programs that
  ignore them read bytes.
- **Ports keep working.** Failures arrive as ordinary `errno` values; `getenv`
  and `setenv` read and write text-content variables as strings, and a non-text
  capability is not presented as a string. Expanded names such as `job://` open
  like any other path.

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
caller's environment, backed by a local repository grant: `ls git://pyxis/branches`,
`cat git://pyxis/commits/main/tree/src/main.c`, `json+git://…` with jq. A branch
resolves to a commit when a resource opens and stays fixed for consistent
reads. Names grant no repository authority. It must not drive current
interfaces.

## Response cache

A later userspace cache may keep complete, validated responses under a byte
budget, evicting by LRU and obeying HTTP caching rules (`Cache-Control`,
validators, `Vary`, authenticated responses; RFC 9111). A local time limit never
authorizes forbidden reuse, the environment and credentials define the sharing
boundary, and eviction never invalidates storage still held by open handles.
Partial bodies never satisfy another open.

## Open questions

1. **Providers:** how a provider learns its client exited and a client sees its
   provider gone; whether a client may hold both a provider's and the kernel's
   handle for one protocol; and the cost of interposition, measured against
   direct kernel delivery before committing a protocol to it.
2. **Shared memory:** the [accepted native region and revocation contract](shared-memory.md)
   is assigned; its implementation/qualification remains pending.
3. **Provider policy:** caller isolation, credentials and cache reuse for any
   shared provider; asynchronous provider work and cancellation (see
   [HTTP responsiveness debt](https://git.internal/PyxisOS/pyxis-os/issues/1453)).

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
