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

- **Order:** milestone A, then B, then C. Each is usable on its own. Owner
  exception, 2026-10-15: bring only C9's capability-output handoff forward as an
  A5 prerequisite; general pipe integration and qualification stay in C9.
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
and hands-free Remote method on 2026-10-14; Luna builds; the orchestrator runs
from horse. **Native baseline gate satisfied, 2026-10-14**: complete record
`/shared/present/capability-baseline/results/A-20261014-124726/` (`DONE`),
retaining the first complete timed/pipe cohort as primary and the second as
corroboration. Three matched inspection boots each measured 2 fully backed
startup pages, **8,192 bytes**, for `boot://session.pxe` in `development`.
The run sheet records exact revisions, configuration, medians/ranges and limits.
Task 1's baseline merge prerequisite is closed; its implementation qualification
and review remain separate. Repeats after A and C remain due. No implementation
checkbox is completed by this scope note.

Merge and qualify A1 → A2 → A3 → A4 → A5, then B6 → B7 → B8, then C9.
Before A5, delta delivers the two bounded kernel prerequisites below, queued
behind #859: capability-output handoff and retention accounting/failed values.
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
whose required baseline gate is now satisfied; integration retains the
A1 → A2 → A3 order. ABI field ordering and helper spelling are routine choices.

### Scope decisions

1. **Baseline method — accepted 2026-10-14.** Existing-tool Lua OPEN,
   launch-through-exit, descriptor OPEN and pipe throughput, plus the full
   checkpoint-observed startup allocation of an identified live process.
   The gate and after-A/after-C repeats are hands-free in the stock Remote space
   via beacon/listen `t14`; debug inspection is driven from horse. The complete
   native gate is satisfied by the completed record above; the pilot is separate.
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

### Task 3 error-body contract (accepted, 2026-10-14)

Error bodies are native immutable FILE snapshots. The issuer supplies bounded
bytes once; the kernel holds the sealed snapshot and describe returns a READ-only
native grant. Exported/provider-served bodies are not accepted. Previously
returned body snapshots remain readable after provider exit, independently of
its controller and lineage, as copied bytes do. This replaces the proposed
error-body lineage derivation; ordinary OPEN children still inherit lineage.
Descriptions cannot form body-reference cycles. Snapshot publication/capture and
retirement are coordinated with delta's merged export code. The implemented ABI
and limits belong in the [provider interface](../interfaces/file-providers.md).

- [x] **1. Kernel forwarders.** A forwarder object with *retarget* (same protocol/representation
  only) and *cut* rights, held by its creator; revocable copies with
  derivation tracking, so a cut also cuts copies made through it; holders see
  *denied*. Service slots are forwarders with both rights.
- [x] **2. One environment table.** The startup record's standard streams, named
  resources, scheme roots, namespace grant and string environment become one
  table of named entries, each a capability or an inline text value. Launch
  passes exported entries only. `name://rest` resolves through it; libc keeps
  descriptors 0–2 and `getenv`/`setenv` over text entries; bundle grants become
  declared required or optional entries. Kernel namespaces, the namespace grant
  and the `namespace` commands are removed; `service start` returns a slot from
  task 1.
- [x] **3. Typed open and `describe`.** The generic OPEN with intent flags and no
  protocol fields, open on held capabilities, typed results checked against the
  kernel's protocol, the `describe` operation and the outcome classes (ok, not
  found, denied, I/O error, too large, aborted, uncertain) with their `errno`
  mapping. Covers native files and directories and the `text` provider. The
  path helpers' URI-spelling refusals are removed.
- [x] **4. Libc.** `fopen` dispatches on the returned type; transactions commit
  on close and on explicit `fflush`, and an abandoned transaction fails loudly;
  `pyxis_describe()` and the error-body call; failures as `errno`.
- [ ] **5. Shell.** `name=value`, `$name` expansion as decided above, `export`,
  `unset`, `$(…)` capture with retention (64 MiB per value, 512 MiB per session,
  *too large* past either), `> $x` and `>> $x`, the `describe` command, a slot
  from `service start`, and revocable copies with `revocable`/`revoke` and
  explicit private `<destination>.control` entries. The accepted kernel
  prerequisites and remaining scope decisions are below.

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

## Task 1 scope (delta, 2026-10-14)

Task 1 implements the [forwarder interface](../interfaces/forwarders.md):
REVOCABLE copies and stable service SLOTs, separate copyable controllers,
bounded lineage and native/provider admission. CUT replaces public export
WITHDRAW; provider-private RETIRE/ACK remains. Allocation and destruction stay
BSP-owned, with short structural guards and unchanged VM/activity leases.
The [qualification record](../development/experiments/capability-forwarders/README.md)
distinguishes source inspection, QEMU and unrun cases. The
[native baseline](../development/experiments/capability-model-baseline.md) is
merged in #825; repeat its profile after A and C.

**Owner-accepted lifetime rules, 2026-10-14:** last controller release auto-cuts
revocable copies, including teardown. Slots retain their last target after
manager loss; a supervisor keeps a copyable controller for takeover. No recovery
capability or implicit holder management is introduced. Opened descendants
inherit lineage. Queued work fails DENIED; delivered/started work completes
under its ordinary deadline, stop and provider-lifetime rules. Bytes and effects
already produced are not revoked.

### RETARGET representation (accepted owner decision, 2026-10-14)

Retargeting requires the same attested protocol and representation: native to
native or exported to exported. Native/exported switches return WRONG_TYPE
without changing the slot. No kernel FILE bridge is provided in this slice.
Revisit with the interchangeable-providers prerequisite **provider-backed device
handles**, designed with the compositor; see
[technical debt](../technical-debt.md#forwarder-retarget-representation).

### Task 2 coordination

Beta's lookup contract is agreed: a retained capability with unchanged resource
and transport rights, or owned immutable text, never movable-table pointers.
One caller-bound process table serves LOOKUP/SET/UNSET and owned SNAPSHOT;
launch captures exported entries and explicit child overrides without mutating
the parent. Canonical headers are `abi/environment.h` and userland `environment.h`;
helpers are `environment_lookup`, `environment_set_capability`,
`environment_set_text`, `environment_unset` and `environment_snapshot`.
The concrete layouts and ownership rules are in the
[environment reference](../interfaces/environment.md). A3 can add optional native
immutable-FILE materialization from an already captured TEXT generation, outside
the table guard; ordinary TEXT lookup performs no eager file copy or second lookup.

## Task 2 scope (delta, 2026-10-14)

**Implemented; defaults accepted 2026-10-14.** Branch
`kernel/process-environment`; the qualification record identifies its baseline
and final inputs. Task 1 is already checked. One process-owned environment replaces
startup resources, roots, stream bindings, string variables and kernel
namespaces. A caller-bound native object supplies the agreed owned
LOOKUP/SET/UNSET/SNAPSHOT view. Table growth, replacement and teardown stay
BSP-owned; guards cover capture/publication, not allocation, grant callbacks,
user copies or waits. Siblings share this table. Immutable captures keep their
values across replacement/unset; live-table authority is not delegated to children.

Launch captures exported entries, then applies explicit child-only overrides
and attenuation without changing the parent. Bundle required/optional entries
and trusted handoffs preserve their current grant restrictions; private entries
are not inherited implicitly. Cwd remains an owned directory chain; argv and
the caller-only FS bootstrap remain startup machinery, not new name channels.
Libc uses the same table for text mutation and caches only immutable captures,
preserving its borrowed `getenv` and descriptor ownership contracts. Namespace
ABI/objects/helpers/commands are removed, with in-tree callers migrated together.
The service publication handshake yields a task-1 SLOT/controller pair; this
does not implement task 5's assignment/capture syntax.

The interim HTTP stdin pair uses ordinary `stdin` and `stdin.status` entries,
captured together from a snapshot. Explicit child stream overrides replace/remove
the companion in child staging only; libc checks it when using response stdin.
This preserves the shipped pair without compound values, protected names or
inferred observation authority; entry spelling is routine layout work.

### Accepted task-2 decisions (owner, 2026-10-14)

1. **Service-controller ownership.** Keep the existing interim
   `service start NAME IMAGE` / `service replace NAME IMAGE` interface. Start
   publishes exported `NAME` as the slot and private, non-exported `NAME.control`
   as its controller, atomically; reject either-name collision without partial
   publication. Replace uses that held controller to RETARGET, not a new slot.
   Trusted session successors explicitly stage controller copies as private
   child entries; ordinary jobs do not receive them. These are ordinary,
   unprotected names in the one table, not a second management-name map.
2. **Admission policy.** Tunable 1,024 entries and 1 MiB aggregate
   owned name/text bytes per live table or snapshot, with no additional per-value
   cap. Values beyond endpoint payload size use the native buffer interface.
   Mutation/snapshot exhaustion fails before publication and preserves old
   values; child capture/serialization retains its existing 64 KiB budget and
   fails before making any child runnable. These are explicit policy limits,
   not ABI or the namespace's former 64-binding contract.

The [unchanged-code local baseline](../development/experiments/process-environment/README.md)
is captured on 1/4-CPU QEMU before code; the ordinary image build and existing
IPC/OPEN/launch/pipe commands passed. Then qualify owned lookup, concurrent mutation/snapshots, exported-only launch,
overrides, slot replacement/cleanup and live/remote mux on 1/4-CPU QEMU/GDB.
The owner's native baseline remains the reference for matched after-A/C runs.
Delivery includes dependent userland/ports changes before the parent pin and
updated interface references. Startup/launch representation changes require
all target binaries to rebuild against the matching SDK, including separately
published toolchain-volume binaries. LLVM #9 migrates the target runtime to this environment, without code-generation
changes. The existing hosted build requires source, SDK provenance and installed
compiler revision to match. The owner has published the matching `df87791` image;
it is adopted with the qualified fork pin. Merge that fork fast-forward before
the userland/ports dependencies and parent integration.

### Accepted lifetime decision: standard streams (owner, 2026-10-14)

**Accepted:** stdio captures the original `stdin`, `stdout`, `stderr`
and optional `stdin.status` entries with non-reused publication identities.
Closing/replacing an original standard descriptor conditionally removes its
still-matching entries, then releases the local descriptor alias. A changed or
absent entry is untouched; each companion is checked independently in one
atomic cleanup. Duplicate descriptors have their ordinary independent lifetime;
reused descriptor numbers carry no original-entry cleanup identity. `dup2` does
not implicitly SET an environment name: child redirection uses the existing
explicit descriptor overrides. Table cleanup runs outside descriptor locks,
and native release is still attempted if cleanup fails, preserving close's
invalidates/no-retry rule. Captured snapshots deliberately retain authority and
must be released before application execution or unrelated waits.

All three task-2 decisions are implemented in #832. The matching df87791 compiler
image is adopted; full Clang/LLD links, 1/4-CPU environment stress and hosted driver
qualification are recorded in the
[task record](../development/experiments/process-environment/README.md). The temporary shell HTTP `@name` registry remains private
operation state until A5/B6, not a generic resolver or second process environment.

## Task 4 scope (accepted, 2026-10-14)

A4 dispatches libc opens from A3's attested typed result, adds `pyxis_describe()`
and explicit error-body access, applies the outcome-to-errno table, and folds in
5c. Unsupported types fail; directories do not become byte streams. Preserve
allocation-before-mutation and descriptor/FILE ownership. Exact-stream `fflush`
and close commit once; implicit drains/flush-all only stage. `w+` reads its reply,
later writes fail, and lost submissions are never retried. Authority remains
in declared grants; TLS stays in the provider. No shell or B6 policy work.

Pre-implementation source inspection (2026-10-14): `descriptor_open()` classified
HTTP by spelling; `fclose` drained/released without submission; `stdio_finish`
ignored flush errors and `exit` preserved the requested status. A4 replaces these
paths under the accepted contract below.

### Accepted decisions (owner, 2026-10-14)

1. **Transaction/HTTP boundary.** A4 introduces a protocol-neutral transaction
   interface with independently attenuable staging/commit/abort authority, an
   owned writable staging FILE and a typed readable commit result. Migrate the
   shipped prepared HTTP controls to it, preserving backing/deadlines and private
   observation. Keep the isolated legacy preparation/name route until B6/B8;
   generic OPEN never falls back. The descriptor dispatcher uses attested type.
   Remove `pyxis_http_commit`, migrate C/Lua callers and retire Lua's HTTP adapter
   here as folded 5c. B6 still owns transparent OPEN/GET snapshots/redirects/DELETE,
   B7 `http prepare`, B8 the remaining interim surface. Lua adapter retirement is part of A4
   through folded 5c, rather than waiting for B8.
2. **Inspecting failed opens.** Ordinary `fopen` returns NULL/errno and releases
   its provisional result. An opt-in `pyxis_fopen` result output retains failure
   description/body under explicit caller ownership; its body helper yields an
   independent read FILE without another OPEN. `pyxis_describe(FILE *, ...)`
   queries a held stream without moving its cursor. No last-error slot or fresh
   fetch. This preserves ordinary port behaviour.
3. **Alias close and exit.** final owning descriptor-alias close commits
   the shared transaction once; earlier alias closes only drain/stage. Exact-stream
   flush may commit earlier. Normal exit stages all buffers then commits clean
   transactions; an existing nonzero status is preserved, while finalization
   failure changes zero to EXIT_FAILURE and reports through available stderr.
   Requested nonzero status alone does not prove producer failure. FILE errors
   block commit until explicitly cleared; native staging failures remain latched.
   `_Exit`, crash and abort do not submit; retained observers see failure.
   This respects shared dup ownership and prevents silent exit success.

### Baseline and qualification

Before code, measure current main `a41f7fe4` with its pins and `df87791` builder:
nested q35/KVM, 4 CPUs/1 thread, 2 GiB, VirtIO net/RNG, idle host, alternating
baseline/changed boots, three warmed cohorts and ranges. Existing Lua OPEN
intervals for `boot://share/iobench.bin`/`text://welcome`, plus verified 1 MiB
native/RAM `iobench read`, cover libc dispatch. Existing local HTTP GET/staged-POST
exercises cover verified 1 MiB completion and staging/commit separately, including
buffered drains. Use existing tools; no new benchmark framework.

Nested timings do not replace the native A gate. Preserve the bold network-target
requirement/ThinkPad TCP reference, report any HTTP read-path cost, and leave
matched native comparison to the owner batch. Route alias/exit/abandon, body
retention, stale/cut grants, staging faults and lost-reply repros to Daybreak
through the owner. Ordinary builds/QEMU C/Lua checks cover types, errno, flush-all
and exactly-once submission. Userland/ports PRs precede parent pins/interface docs.
Task 4 implementation is assigned on 2026-10-14; baseline precedes code.

### Accepted Lua cleanup decision (owner, 2026-10-14)

Pending transactions are abandoned during Lua GC and error-unwind cleanup:
no submission, and retained observers see failure. Report real close errors
where a diagnostic or Lua error can reach the caller. Explicit flush/close and
clean normal exit preserve the accepted rules; automatic cleanup must distinguish
clean shutdown from abandoning a pending producer.


### Task 4 qualification (2026-10-15)

Delivered and qualified at parent `68ba1f03`, userland `f239d47`, ports
`0e76012`. Daybreak found no new defect after the unread-close fix: live-reader
EOF/framing, close/write and dup2/close/reopen races, and immutable failed-OPEN/
COMMIT error bodies across CUT/receiver shutdown and handle-table exhaustion
passed. The [qualification record](../development/experiments/capability-libc.md#daybreak-requalification--2026-10-15)
keeps counts and limits: allocation failure was source-inspected only, and
receiver shutdown exercised provider loss rather than a separate provider process.


## A5 kernel prerequisites: output handoff and retained values

Owner-assigned to delta, 2026-10-15; merged and QEMU-qualified. Handoff [#874](https://git.internal/PyxisOS/pyxis-os/pulls/874) /
[userland #271](https://git.internal/PyxisOS/pyxis-userland/pulls/271) remain
separate from retention [#879](https://git.internal/PyxisOS/pyxis-os/pulls/879) /
[userland #273](https://git.internal/PyxisOS/pyxis-userland/pulls/273).
The [owned output interface](../interfaces/output.md) is authoritative; records
are [handoff](../development/experiments/capability-output.md) and
[retention](../development/experiments/retained-values.md).

- [x] **Output handoff.** FILE-compatible producers accept bytes or one actual
  owned grant plus existing observation. Preserve wrappers, type, masks,
  failure and lineage; reserve before move/collection, never consume borrowed
  descriptor handles or retry uncertain transfers. Shared libc and eligible
  single-source `cat` use the primitive, without command-name special cases.
  **After this prerequisite the owner can:** capture a re-readable FILE larger
  than 64 MiB without byte copying or another retention charge, transfer a failed
  result unchanged, and capture byte-only output through the native SDK.
- [x] **Retention and immutable failure.** Trusted startup carries private
  factory authority to the root configured session. It creates one account;
  successors, including local/Remote mux panes and connections, receive only
  that shared account. Reserve native RAM backing charge before growth; keep
  origin account/charge through COPY, export, unset and shell exit until final
  backing release. Retention replaces the unaccounted output constructor.
  **After this prerequisite the owner can:** inspect exact value/session
  boundaries, immutable TOO_LARGE reads after crossing them, and charge
  drainage after the last backing holder releases it.

### Accepted decisions (owner, 2026-10-15)

1. **Mixed output fails capture.** First nonempty byte write selects BYTES;
   successful handoff selects VALUE. Mixing or a second value rejects the
   conflicting grant unchanged and latches capture failure, with no successful
   prefix/result. Zero writes select neither. Generic failed admitted mutations
   also fail capture; legitimate retention exhaustion instead publishes only
   its immutable failed FILE.
2. **Collector stays process-local.** Noncopyable/nondelegatable; excluded from
   environments, mutable forwarder targets and handoff payloads. Producers
   remain transferable. Final logical collector close/cancel drains queued
   ownership outside the guard, independently of storage-only references.
   Delegated collectors await a demonstrated C9 consumer.
3. **Charge logical length.** Include sparse holes, exclude page/index rounding.
   Overwrite adds no charge; growth reserves the delta; shrink returns it.
   Exactly at the configured limits succeeds. Defaults are 64 MiB/value and
   512 MiB/session, set once by session configuration, not ABI constants.

Shell capability substitution, account-backed streamed-source retention and
paired management publication still belong to beta's A5 consumer integration.
General pipeline value transport stays C9. Existing pipeline/Lua byte capture
keeps its explicit polled policy until replaced there. No compiler-container
rebuild or native performance claim. #859's dock packet remains independent.
## Task 5 scope (2026-10-15)

A4 is merged at `fecd1439`. The following contract is accepted by the owner on
2026-10-15. The kernel prerequisites above are merged; shell capture integration
is assigned on top of them. Keep TEXT/CAP expansion, explicit inheritance, failed values,
64 MiB/value and 512 MiB/session policy, and controller-close rules.

### Concrete scope and inspected gaps

Use the one [environment table](../interfaces/environment.md) for assignment,
value copies, `export` and `unset`. Explicit `$x` CAP operands carry their captured
entries in per-command child staging, preserving bundle/profile attenuation;
other private entries and the parent's export flags stay unchanged. TEXT adds
bytes, not authority. Add typed `$(...)` and inline temporaries, `>`/`>>` through
typed OPEN, `describe`, service values and revocable copies. Update shell,
SDK/ports consumers, init scripts and interface references together. Existing
private HTTP continuation stays isolated until its assigned B6–B8 replacement.

At scope time, the [parser](../../userspace/shell/parse.c) lacked an expansion tree;
[service launch](../../userspace/shell/launch.c) publishes holder/controller pairs
but returns status, not a value. The [capture SDK](../../userspace/libc/include/pyxis/capture.h)
permits polled overshoot and capped reads, not hard retention or an owned result
FILE. [RAM FILEs](../../include/kernel/object/file.h) share backing until final
release but lack retention accounting. The [pipe ABI](../../include/abi/pipe.h)
carries bytes only and [cat](../../userspace/cat/main.c) always copies bytes:
accepted `x=$(cat tmp://big.json)` no-copy/no-limit needs capability output transport.

### Accepted contract (2026-10-15)

1. **A5/C9 dependency: bring the required C9 capability-output handoff
   forward as an explicit prerequisite, owned by delta.** Transfer a held result
   to capture without reading/copying it, while byte-only producers keep working.
   This is a limited exception to A → B → C; general pipe integration and
   qualification remain C9. Do not special-case command names or silently
   copy/limit re-readable values.
2. **Retention ownership: native RAM FILE retention accounting and
   immutable failed values as a bounded kernel prerequisite with delta.** Trusted
   startup supplies declared retention authority, sharing the configured session
   account across shells/panes. Reserve charge before growth; keep the originating
   session/space charge with backing until its final holder releases it, across
   export, unset and shell exit. Readers get read-only values, not account control.
   Exactly-at-limit succeeds; past either limit binds a TOO_LARGE failed value,
   reports failure and suggests a file, without a successful truncated prefix.
   A shell counter cannot observe an exported final holder.
3. **Management surface: `revocable`/`revoke` with an explicit private
   `<destination>.control` entry**, following A2. `mic=$(revocable $microphone)`
   binds a holder plus `mic.control`; `revoke ${mic.control}` uses its CUT grant.
   `svc=$(service start IMAGE [ARG...])` returns a slot plus `svc.control`.
   Publish pairs atomically; companion collisions refuse, preserving old entries.
   Holder copy/export does not delegate its controller. Holder unset/replacement
   leaves the separate control entry intact; explicitly release/copy it by name.
   Last-controller release cuts revocable holders, but slots retain their target.
   Kernel revocation semantics stay fixed.

### Capture integration: remaining service-lifetime decision

**Proposed, not accepted (2026-10-15): paired service capture is leaf-only.**
`svc=$(service start IMAGE ...)` must preserve the old bindings if publication
fails, and drain the newly started provider. The current plain service profile
usually supplies no launcher, but bundle selection and explicit CAP operands can
still put LAUNCHER grants in the final request. A process observer cannot stop
those descendants. A temporary execution subgroup would drain them, but dropping
its final CONTROL after successful publication also stops the service; retaining
it needs an explicit durable owner and release rule.

**Default:** inspect the complete prepared request and refuse a paired service
start carrying any direct LAUNCHER grant, before redirects or provider launch.
Do not silently omit a declared grant. Use the existing process observer to
terminate and wait on publication failure; after publication release that
observer without terminating the provider. Ordinary named bootstrap service
startup stays unchanged. Other command captures use an owned temporary subgroup
and wait for its retirement. This bounds the new paired surface without adding
a hidden supervisor.

**Alternative:** first settle an explicit durable execution-group control entry
and its lifetime after publication, then allow service captures that can spawn
children. That changes the paired management contract and needs owner review;
no such entry or implicit supervisor is implemented here.

Capture implementation stops for this decision. A5 remains unchecked. No
substitution uses the old polled/capped capture SDK as a fallback.

### Observation entry layout under the accepted A2/A5 rules

An optional observation handed out with a named capture is an ordinary private
`<destination>.status` CAP entry. Publish it with the primary in one SET batch;
REQUIRE_ABSENT on the companion refuses a collision without replacing either
old binding. It is separate authority: copying, exporting or unsetting the
primary does not copy, export or unset the observation. A matching suffix never
proves a relationship and `$x` redirection must not discover or delegate
`x.status` by name. An inline result may explicitly stage its directly held
observation along with its temporary primary. This follows A2's separate-entry
model, without compound values or a second shell authority table.

### Kernel prerequisite delivery

The [output handoff and retained-value prerequisites](#a5-kernel-prerequisites-output-handoff-and-retained-values)
are merged. A5 uses their account-backed constructor and immutable failed values;
it must not restore unaccounted byte storage or the old capped-read fallback.
General C9 pipeline integration remains outside this task.

### Accepted parser, temporary lifetime and failure (2026-10-15)

1. **Parser and quoting: literal quoting plus typed whole-word CAP
   expansion.** Expand `$name`, `${full.name}` and nested `$(...)` unquoted or
   in double quotes; single quotes and escaped `$` stay literal. `$name` uses
   letters/underscore followed by letters/digits/underscore; braces accept every
   valid environment name. TEXT may concatenate with literal pieces, without
   field splitting, globbing or reparsing. A CAP must occupy the entire word,
   quoted or not; concatenating it with text is an error rather than losing its
   authority. `copy=$job` copies its typed value; `export copy=$job` explicitly
   exports it. Add sequential `;` as used in the design; assignments are standalone
   commands or `export` operands, not command-prefix overrides. Keep the current
   pipeline/background limits; no `&&`, `||` or positional expansion in A5.

   ```sh
   title="two words"; echo "$title"       # one TEXT argument
   copy=$job; cat "$copy"                # typed entry, not reply bytes in argv
   revoke ${mic.control}                # dots require braces
   echo "prefix:$job"                   # rejected: mixed TEXT/CAP word
   ```

2. **Temporary controllers: paired management producers require a
   named assignment destination.** `mic=$(revocable $microphone)` and
   `svc=$(service start IMAGE [ARG...])` publish the holder and private controller
   atomically; reject inline `recorder $(revocable $microphone)` before creating
   a forwarder or launching a service. A plain inline CAP temporary has no hidden
   controller: retain it through child grant capture, then release the shell's
   temporary reference; child copies follow ordinary lifetime. Preflight names,
   companion collisions and admission before work; if publication still fails,
   release unpublished grants and stop/drain any newly owned service startup.
   Keep old entries intact; do not claim external side effects were rolled back.

3. **Command failure: publish evaluated captures, preserve old values
   on preparation/publication failure, and stop an enclosing launch on failed
   inline substitution.** Parse and validate syntax first. If no capture starts,
   a bad name, missing reference or admission failure leaves the destination
   unchanged. Once capture evaluation begins, bind its output even on command
   failure and report nonzero status: an unchanged failed CAP keeps its outcome;
   a program that consumed an error leaves its actual output, possibly empty. A launch/OPEN
   failure with no produced result binds a failed value, not successful empty
   output. Retention overflow binds TOO_LARGE. A publication failure preserves the
   old binding and releases the unpublished result. Captured pipelines check every
   stage for failure; ordinary pipelines keep their current last-stage rule. A failed inline
   substitution prevents the outer command/pipeline from launching; earlier
   completed substitutions or file side effects are not undone. Cancellation
   stops/drains the owned subtree and abandons unfinished transactions; committed
   effects remain. Keep A4's close/exit contract, including that nonzero exit alone
   does not prove a producer failure. Do not retry failed submissions.

### Assigned shell work and dependency boundary

Implement literal/TEXT assignment and expansion, typed value copies, explicit
per-command CAP delegation, `export`/`unset`, sequential `;`, typed redirection
including append, `describe` and explicit `revoke` against A2–A4. Parse
substitutions and reject anonymous management producers before effects.

The kernel prerequisites are merged. General command-output capture, inline
output temporaries, account-backed retained output and immutable failed-result
publication are now assigned. Paired management capture uses those same failed
values; the service-lifetime decision above remains pending. Until integration
is complete, unsupported forms refuse before launch or redirect OPEN. Do not
substitute the old truncating capture SDK, copy re-readable values or claim A5
complete early. General C9 integration and the B6–B8 HTTP transition remain
outside this task.

### Accepted child-binding collision rule (2026-10-15)

An explicit `$x` must pass `x://` with that captured entry. Launch also selects
standard dependencies from retained startup grants and bundle-owned entries.
Those can differ: `clock=$home; ls $clock` selects a DIRECTORY, while today's
shell still stages its original CLOCK under `clock`; a bundle similarly installs
its own `app`. Matching protocol/masks or numeric handles cannot prove identity.

**Refuse a different-source same-name child binding before redirects
or launch**, reporting the name and suggesting an explicit private copy, for
example `listing=$clock; ls $listing`. Reuse and narrow a shared captured source
when its provenance is known; never union rights. Assignment/lookup/shadowing in
the parent's environment remain allowed. No generated argv aliases, implicit
renaming, override or parent-table mutation. The owner accepted refusal with the
copy-to-another-name hint. Shell implementation may proceed within this contract.

### Baseline before implementation

The [pre-code nested baseline](../development/experiments/capability-shell.md)
is captured on main `146de8e4`, before shell or prerequisite changes: existing
Lua CLOCK-timed native/text OPEN and small launch-through-exit runs, `iobench`
native/RAM/pipe throughput, and Remote `echo`/`echo | cat` completion intervals.
Three cohorts passed; the record gives pins, commands, configuration, ranges and
host-load limits. Repeat unchanged/changed boots interleaved after implementation;
new syntax has no before-time and must be reported separately. Use ordinary
QEMU/debugger checks and route adversarial/lifetime repros through the owner to
Daybreak. Preserve the identical after-A/after-C
[native run sheet](../development/experiments/capability-model-baseline.md)
repeats. A5 remains unchecked; the baseline is not implementation qualification.
