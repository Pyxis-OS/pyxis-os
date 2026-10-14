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
- **Measure natively.** Before task 1 merges, record a baseline on the ThinkPad: open
  latency for a native file and a `text://` resource, launch time of a small
  program, startup-record size and pipe throughput. Repeat after milestones A
  and C, with the revisions and samples recorded here.

## Integration checkpoint (2026-10-14)

The [native baseline run sheet](../development/experiments/capability-model-baseline.md)
uses fresh main `09a51b9a` and existing tools. The owner accepted its boundaries
and hands-free Remote method on 2026-10-14; Luna builds; the orchestrator stages and runs the host runner from horse. Two native timed/pipe cohorts on `09a51b9a` are recorded; the first is
primary, the second corroboration. One startup inspection observed 8,192 bytes;
three matched inspection boots remain **pending**. The older `3b714754` pilot is
separate. Task 1's merge gate remains open. No implementation checkbox is completed
by this scope note.

Merge and qualify A1 → A2 → A3 → A4 → A5, then B6 → B7 → B8, then C9.
Delta owns A1/A2/C9; beta owns the remaining changes and integration. Dependency
PRs land before their parent pins; each task replaces its old consumers and
updates the corresponding interface reference. Parked HTTP 5b is not resumed
independently, and 5c's implementation is folded into A4 with the settled
close-commits rule, replacing its old close-only-stages behavior.

### Task 2 boundary agreed with delta (2026-10-14)

Use one shared SDK environment view for native callers, libc and launch. Lookup
by exact name captures an owned capability reference with unchanged rights, or
owned immutable text, before releasing the table guard. Never retain pointers
into movable table storage across OPEN or child preparation. A child receives
an owned snapshot of exported entries only; a replaced/unset entry cannot alter
an already captured reference. Text opens read-only and contributes no directory
or provider authority. Relative paths retain the native cwd capability chain.

Delta's caller-bound environment object supplies LOOKUP/SET/UNSET and owned
SNAPSHOT enumeration. LOOKUP replies use a tag and metadata: a capability reply
installs one owned local handle with unchanged resource/transport masks; a text
reply copies into caller storage, reports required length on insufficient space
and publishes no partial text. A size retry may observe a later complete value;
a held SNAPSHOT instead fixes the generation for enumeration and launch.
Launch must allow owned, explicitly selected per-child stream/entry overrides
and attenuation against that snapshot. Existing redirections and pipelines must
not SET/UNSET the parent's live table temporarily. Overrides are child staging,
not a second live environment or a grant to enumerate the parent's unexported
entries; the child receives only the explicitly delegated selection.

Task 3 needs exact, case-sensitive entry routing (including existing `_`, `+`,
`-` and `.` names), no protected names, no URI-prefix normalization, and no table
guard held during provider OPEN. LOOKUP copies and newly opened child capabilities
preserve forwarder lineage (owner accepted 2026-10-14). Final controller release
automatically cuts revocable copies, while service slots retain their target;
controllers are copyable and supervisors retain a controller copy. Retargeting a slot is not frozen by a table snapshot. Type and
both authority masks remain kernel-attested; cached metadata promises no liveness.

Keep text's byte length/NUL convention explicit, return capacity failures without
partial text or leaked handles. Child serialization enforces the existing total
startup budget; any separate table/snapshot admission limit must be explicit.
Generic OPEN remains within the current 4 KiB endpoint payload
and four attachments; these are not a limit on an environment text value. Do not
carry forward the namespace's 64-binding limit as an implicit table contract.
The helper header and function names remain delta's routine ABI layout work.
This agreement creates no second environment table or whole-map lookup grant.

### Task 3 scope: typed OPEN and describe

The shared SDK accepts a name or a borrowed held capability, intent and an
absolute deadline, and returns an owned typed result. `name://rest` performs
one environment lookup and opens the opaque remainder on that captured target;
a directory uses native component lookup, while a provider interprets its own
remainder. A held FILE reaches that object only; its OPEN does not re-resolve a
remembered name. The input capability is not consumed, and no failure or timeout
causes automatic retry, rediscovery or replay.

The generic wire request contains only READ/WRITE intent, CREATE, EXCLUSIVE,
TRUNCATE and APPEND flags, bounded target bytes and the absolute deadline.
Endpoint delivery uses that same deadline; it is never restarted. No HTTP origin, method, headers, trust, redirect or budget fields.
Targets reject unsupported intents rather than emulating success; CREATE on a
held FILE without a name is refused, while truncation needs its WRITE authority.
Native files/directories and `text` are A3's implementations. Later transaction
types use the same typed-result shape, not a parallel OPEN.

The result owns its resource and any separate readable error-body grant; it
carries the outcome and bounded descriptive fields. Validate extents, counts,
attachments and declared type against kernel-attested protocol/rights before
publication. Issuers choose granted rights; the client never widens them. Failed
results remain failures when read, not empty files or error bodies disguised as
success. Admission/local validation failures remain explicit if no result can
be allocated. Every provisional attachment is released on validation failure;
returned ownership survives closing the lookup reference.

`describe` returns attested type/rights, outcome, bounded human detail and declared
media type, optional length, re-readable/complete flags and an optional owned
readable error-body capability. It exposes no URI, scheme, provider identity,
HTTP code field or headers. Descriptions neither grant authority nor prove
content; absent size stays unknown. Ordinary FILE_INFO identity/time remains
available to native file consumers, without synthesizing it for providers.

| Resource outcome | Libc mapping in A4 |
| --- | --- |
| ok | success |
| not found | ENOENT |
| denied, including a cut forwarder | EACCES |
| I/O error, including incomplete body or timeout | EIO |
| too large | EFBIG |
| aborted | ECANCELED |
| uncertain | EIO, never retry |

Malformed arguments, unsupported intent/type and memory/admission failures keep
their local errors (EINVAL, ENOTSUP, ENOMEM or the existing limit error), rather
than pretending to be a successful resource. No protocol number is parsed to
choose an outcome or errno.

A3 removes the native/text FILE-only OPEN assumptions and URI-spelling refusals,
updates all their SDK/provider callers and documents the ABI in
`docs/interfaces/file-providers.md` and the affected capability references.
A4 owns libc dispatch and transaction close; A5 owns shell syntax. Neither is
quietly delivered by A3. The two transition defaults below are accepted; task 3
is assigned for implementation on 2026-10-14. Its PR does not merge before task 1,
whose merge gate still requires the complete baseline; integration retains the
A1 → A2 → A3 order. ABI field ordering and helper spelling are routine choices.

### Scope decisions

1. **Baseline method — accepted 2026-10-14.** Existing-tool Lua OPEN,
   launch-through-exit, descriptor OPEN and pipe throughput, plus the full
   checkpoint-observed startup allocation of an identified live process.
   The gate and after-A/after-C repeats are hands-free in the stock Remote space
   via beacon/listen `t14`; debug inspection is driven from horse. The complete
   native gate remains pending; the pilot does not close task 1's merge gate.
2. **Non-blocking exported describe — accepted 2026-10-14.** The issuer
   publishes a bounded description stored with its exported object; describe
   reads the latest published snapshot without calling the provider. Type,
   rights and cut state come from the kernel; unknown fields remain unknown.
   Snapshot storage and an optional error-body reference follow object ownership
   and retirement. Coordinate storage with delta's forwarder/export work.
3. **Legacy HTTP transition — accepted 2026-10-14.** Isolate the existing HTTP
   continuation request/reply as an explicitly private legacy operation until
   B6, adapting every bridge caller. Retire the old generic native/text OPEN
   immediately. New typed callers never fall back to the legacy route. A4 owns
   libc dispatch and transaction semantics; A3 preserves shipped HTTP solely
   through its private continuation path, without keeping two generic OPEN ABIs.

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
