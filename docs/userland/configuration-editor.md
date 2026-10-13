# Configuration validation and delta editor

Installer task 4 supplies the shared static checker, declared-grant editor and
complete-set publisher/replay on scratch ESPs. Installed startup and ordinary
Install/Update are unchanged until task 5.

Run the ordinary `config` bundle:

- `config validate`: literal/typed checks and merged volume/space provenance;
  CPU presence is checked with its optional read inventory. Other unavailable
  machine checks are reported explicitly. The loader preview freezes a complete
  committed triple or uses complete archive defaults.
- `config plan`: per-entry review on stderr and one framed data plan on stdout
  when issuer evidence is available. It binds the target and all consumed inputs.
- `config edit`: existing libterm prompts for Keep/Apply/Adopt/Remove and final
  Apply/Print/Cancel. Apply requires a final explicit confirmation.
- `config apply [--confirm=config]`: consume one frame from stdin, revalidate
  exact target/source hashes and choices, then confirm. The flag supplies final
  consent for redirected input; a plan itself conveys no authority.

There is no current-plan slot, call-site wrapper, raw disk/factory grant,
Continuum application or separate machine-settings writer. **Install and Update
require no prior config use or manual configuration.** This task changes neither
installed startup nor ordinary Install/Update. Task 5 supplies installed
publication/activation; missing catalogs/records still allow archive defaults or
printed changes, never “configure first.” ESP versus pool remains owner-revisitable.

## Shared validation and selection

[libconfig](../../userspace/libconfig/) owns bounded literal data parsing and the
same boot/session/network schemas used by startup. Trusted archive/legacy startup
keeps its existing evaluator; untrusted ESP Lua is never executed. Supported data
is a returned table of tables, strings, finite numbers and booleans. Duplicate
keys, unsupported executable Lua and ambiguous ownership refuse automatic edits.
Hostname/scroll validation is reused from machine settings; boot overrides cannot
create a competing machine store or redefine system bootstrap.

The selected ESP files are `EFI/pyxis/config/{boot,session,network}.lua`.
Boot overrides merge whole named volume/space entries with installed archive
defaults. Session/network are complete inputs. The selection owns frozen bytes
and decoded values, with no later ESP path reopen. Whole-triple validation happens
before effects. The scratch helper requires a matching committed record/catalog;
missing/prepared/mismatching state uses the entire archive triple. Rescue always
uses archive defaults. Installed startup still uses its existing pool override
and archive session/network until task 5; personal overrides are not removed.

## Declarations and authority

A deterministic source catalog carries its source revision, stable IDs,
file/schema/name target, required disposition, literal default and optional
replacement/entry move/field rename. The first catalog is generated from shipped
volume/space and session/network defaults and does not invent Limine paths or
migrations. Default/replacement values pass shared schema/merge checks.

Managed spans use actual whole-line `-- pyxis:begin ID` / `-- pyxis:end ID`
comments, or `#` for Limine. Complete entries/separators belong inside them.
Outside bytes are preserved exactly. Existing unmarked entries need explicit
adoption; owner values remain Keep unless replacement is approved. An old catalog
is evidence only when its retained bytes match its committed hash. Missing history
is unknown, never reconstructed from current defaults. Required declined changes
make apply inapplicable; ordinary Update's default/fallback path is separate.

Trusted startup provides selected sources through bundle declarations:
`config_parent`, `config_writer`, `limine_parent`, `limine_writer`, and
`config_control`. Optional directory-source `path` narrows the issued handle to
a subtree (relative, no empty/dot/dot-dot components, at most 255 bytes/16 components).
MOUNT sources cannot use paths. Normal roots remain RO and ordinary children do
not implicitly receive administrative sources. The config bundle requests actual
mutation rights only for its scoped parents and bound write/interval control.

Bound OBSERVE must certify normalized logical GPT disk/ESP UUIDs, ESP type,
attributes and device geometry, in addition to active domain/partition extent.
Held directory metadata must match that control domain. UUIDs are logical
identity, not physical authentication. Missing issuer facts permit validation and
printed diagnostics only. No raw access or caller-authored sidecar substitutes.

## Publication and limits

The mounted native FAT32 executor stages, syncs and read-verifies the complete
triple/catalog and approved Limine changes. Under a non-extending exclusive lease,
it rechecks current and staged bytes, flushes a prepared record, switches only
approved canonical files, then flushes/read-verifies a committed record. The
record names exact hashes plus one previous set and changed Limine recovery.
Old recovery cleanup follows commit and requires recorded names and matching
contents. Limine recovery is manual.

This detects completion; it provides no FAT journal or multi-file atomic switch.
Failure, deadline or uncertainty stops writes and reports phase/recovery artifacts,
without blind retry, rollback or resume. Invalid/prepared existing recovery requires
manual disposition. Reviewed owner edits need not equal prior selected hashes;
current bytes still undergo exact stale-plan checks under exclusive ownership.

Limits: 1 MiB per file/plan input, literal depth 32, 8192 parser nodes, 128 catalog
entries and 256 deltas; completion records at most 4 KiB; one 30-second native
exclusive interval. Configured FAT selection awaits pending GPT identities for
at most ten seconds and refuses ambiguity, never selecting by ordinal.

[Scratch qualification](../../userspace/config-editor/qualification/README.md)
records source builds, interactive QEMU/GDB, committed publication, replay and
recovery checks. No native media or installed ESP edits are authorized
by this record. The [installer milestone](../wip/installer-tui.md) carries accepted
choices and remaining delivery tasks.
