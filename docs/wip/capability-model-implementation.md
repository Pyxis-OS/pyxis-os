# Capability model implementation

Status: **owner-assigned plan, 2026-10-14.** It implements the settled
[capabilities and providers](capabilities-and-providers.md) design. Codex beta
owns the milestone; Codex delta implements the kernel tasks (1, 2 and C) on
separate branches. Each task starts with a short scope note and stops only for
unresolved behaviour, authority or lifetime decisions; ABI layout and other
routine choices need no approval.

Libc, shell, web/HTTP and Git work outside this milestone stays paused until the
task that owns the area is delivered.

## Agreed decisions (2026-10-14)

- **Order:** milestone A, then B, then C. Each is usable on its own.
- **Ownership:** as in the status line.
- **Expansion of `$x`:** text values, from literal assignments such as
  `f="home://notes.txt"`, expand to their text, as in bash. The text is resolved
  through the caller's own environment and confers no authority. Every other
  value (captured `$()` output, replies, prepared requests, devices,
  directories) passes as `x://` with its entry, so `cat $job` reads the reply
  and `echo $job` prints `job://`. Inline `$(…)` used as an argument passes a
  capability, as bash's `<(…)` does; idioms such as `rm $(find …)` do not apply.
- **Remove** works on names. Removing through a held FILE capability without a
  name is refused for now; making `rm` work on both names and capabilities is a
  later item.

## Rules for every task

- **Replace, don't coexist.** Each task removes the interface it replaces and
  updates every in-tree consumer in the same change, across the parent,
  userland and ports repositories, with linked PRs and stated merge order.
- **Interfaces beside code.** Each task updates the matching
  `docs/interfaces/` reference; this document only tracks progress.
- **Measure natively.** Before task 1, record a baseline on the ThinkPad: open
  latency for a native file and a `text://` resource, launch time of a small
  program, startup-record size and pipe throughput. Repeat after milestones A
  and C, with the revisions and samples recorded here.

## Milestone A: the core, with files and text

- [ ] **1. Kernel forwarders.** A forwarder object with *retarget* (same type
  only) and *cut* rights, held by its creator; revocable copies with
  derivation tracking, so a cut also cuts copies made through it; holders see
  *denied*. Service slots are forwarders with both rights.
- [ ] **2. One environment table.** The startup record's standard streams, named
  resources, scheme roots, namespace grant and string environment become one
  table of named entries, each a capability or an inline text value. Launch
  passes exported entries only. `name://rest` resolves through it; libc keeps
  descriptors 0–2 and `getenv`/`setenv` over text entries; bundle grants become
  declared required or optional entries. Kernel namespaces, the namespace grant
  and the `namespace` commands are removed; `service start` returns a slot from
  task 1.
- [ ] **3. Typed open and `describe`.** The generic OPEN with intent flags and no
  protocol fields, open on held capabilities, typed results checked against the
  kernel's protocol, the `describe` operation and the outcome classes (ok, not
  found, denied, I/O error, too large, aborted, uncertain) with their `errno`
  mapping. Covers native files and directories and the `text` provider. The
  path helpers' URI-spelling refusals are removed.
- [ ] **4. Libc.** `fopen` dispatches on the returned type; transactions commit
  on close and on explicit `fflush`, and an abandoned transaction fails loudly;
  `pyxis_describe()` and the error-body call; failures as `errno`.
- [ ] **5. Shell.** `name=value`, `$name` expansion as decided above, `export`,
  `unset`, `$(…)` capture with retention (64 MiB per value, 512 MiB per session,
  *too large* past either), `> $x` and `>> $x`, the `describe` command, a slot
  from `service start`, and revocable copies with revoke (names to settle in the
  task).

## Milestone B: HTTP on the model

- [ ] **6. Transparent HTTP provider.** `httpfs` issues GET snapshots on read, a
  one-shot POST transaction on write and DELETE on remove; it follows redirects
  itself, including 307 and 308 with the body, and drops credentials across
  origins; failures and facets follow the model. REDIRECT, the legacy OPEN
  route and client-side redirect following are removed.
- [ ] **7. `http prepare`.** Issues a prepared-request capability carrying request
  headers (including authorization), the write method (POST, PUT or PATCH) and
  the trust profile. Read is GET and remove is DELETE, with the same headers.
- [ ] **8. Retire the interim surface.** `=> name`, `@name`, `request`, PREPARE as
  a client path, `provider_http_uri()` and the shell and libc spelling checks
  are removed; Lua's HTTP adapter becomes plain `io.open`. Parked task 5b
  (standalone scripts) is rebased onto the model; parked 5c is absorbed by
  task 4.

## Milestone C: capability pipes

- [ ] **9. Pipes carry capabilities.** A pipe can pass its producer's capability
  to the consumer instead of copying bytes, so files and replies travel without
  a copy and a failed capability fails its reader. Byte pipes remain the base
  case, so programs that only read bytes are unaffected.

## Later

- `rm` on held capabilities as well as names.
- The provider-side open questions in the design: provider lifecycle and the
  cost of interposition, shared memory, and provider policy.
