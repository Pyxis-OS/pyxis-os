# Lua language server

Plan and all three decisions accepted (chronium, 2026-10-25). Task 1 supplies
the native runtime. Task 2 supplies the public program package and opt-in
Neovim integration; full feature/native qualification and measured capacity
sizing remain task 3 below.

Goal: install `lua-language-server` and enable `lua_ls` in Neovim, with real
semantic highlighting of `vim.*`, typed API completion and diagnostics.
Text-only word completion does not meet the goal. Navigation follows those
features. Use the existing [clangd integration](../userland/clangd.md),
[native port rules](../development/ports.md) and
[repository boundary](../development/sdk-and-repositories.md).

## Inspected source and build

[LuaLS 3.19.2](https://github.com/LuaLS/lua-language-server/releases/tag/3.19.2),
published 2026-10-13, is the latest release checked on 2026-10-25.
Its immutable inputs are:

| Input | Pin | Licence / first profile |
| --- | --- | --- |
| LuaLS | `b144c872f081373a893b55979142fed651e4425a` | MIT; server scripts, bootstrap, built-in templates and locale |
| bee.lua | `f55b6988e8d9564816a555118b0e76564ef9a283` | MIT; embedded Lua 5.5.0 with its Lua.org/PUC-Rio notice, native serializer and retained third-party notices; fmt is not selected |
| LPegLabel | `912b0b9e8641074408ffc2259e069b188e0c717b` | MIT, including inherited Lua/LPeg author notices; static native parser |
| json.lua | `08095fd2cdfde8f49a5f5b54b35ec7b3d4906a36` | MIT; provenance of JSON Lua code already in the server scripts; no JSON test dependency |
| luamake | `4c4bd16c9b0c1d773402fc15c76367f7899a1f91` | MIT; host generator alternative, excluded from the recommended direct Make build |
| luamake's separate bee.lua | `466c4f071a17ab7c31e402383b0a2097b03535ba` | MIT and bundled notices; host-only if that generator is used |
| EmmyLuaCodeStyle | `8c4289b7617ccdb0b247a6171f111f28ac7ae969` | MIT plus embedded Boost/other notices; formatter excluded initially |

The root has 21 gitlinks. The unselected `love-api`, `lovr-api` and all
`meta/3rd/*` framework annotations/add-ons are bound by that root pin but are
not required, fetched or shipped in this profile. Their licences are not
claimed closed by the server's MIT licence. bee has no nested gitlinks.
JSON's `test/ltest` is excluded. Keep bee's cloudwu attribution in imported
`lua-seri` and the original MIT notice, copyright 2014 codingnow.com, verified
at [lua-serialize d523513c](https://github.com/cloudwu/lua-serialize/blob/d523513c0612cf4442d97116038afd36cf1168be/LICENSE#L1-L20).
That is a licence reference, not a new build dependency or a claim to have
identified the historical import commit.

All inspected archives came through the existing
`https://repo.internal/repository/raw-github/OWNER/REPO/archive/COMMIT.tar.gz`
cache. SHA-256 receipts are retained with the investigation. Required source
archive hashes, in table order for LuaLS, bee, LPegLabel and JSON:

```text
844ed9f36ea5cd9bd1310861fcae73b9859996aee7bf06f3b048805507a63a2c
61f25750910fce54f8e4dc1e3118e831a479bb1f654573e8cee57de4403bb2f4
f30f8039b89a571bdf450d1b82727ddbb3905e58b205ddd0421fad50983b991f
625fc92798e27d8c6597b59b1f8d014744496dc2e1c5117baa5e1a4d286c7b7e
```

## Runtime/platform closure (task 1)

The ports recipe builds LuaLS with a small host Makefile, coherent bee embedded
Lua 5.5.0, static LPegLabel and native C++ bindings. The source runner downloads
only checksum-pinned approved-cache archives. Host generators, target
Ninja, optional formatter and bee's Linux backend are not used. Lua compiles in
its supported C++ error-unwind mode with C API linkage, so binding owners are
released across Lua errors. Reached standard functions use the existing SDK;
no new libc/kernel operation or compiler-container rebuild is required.

The manual runtime bundle keeps scripts, templates and locales under read-only
`app://share/lua-language-server/`. It carries the complete selected licences,
original port sources/patches, runtime-file hashes and actual SDK provenance.
The public program package and editor configuration belong to task 2. The
[task 1 record](../development/experiments/lua-language-server-runtime.md)
separates actual link/QEMU evidence from remaining feature/native checks.

| Adapter | Behaviour and limit |
| --- | --- |
| Filesystem and paths | Native libc FILE/directory operations; scheme-aware joins, classification and lexical normalization. Descriptive absolute paths are not identity proofs. Missing rights/metadata remain real errors/unknowns. No `std::filesystem`, procfs or fake executable path. |
| URI | Neovim-compatible reversible `ROOT://rest` ↔ `file:///ROOT/rest`, percent encoding and exact root/component boundaries. Text does not confer authority. |
| Workers/channels | Real SDK shared-process threads, separate Lua states and copied lua-seri messages. Typed channel readiness and condition waits replace socketpair/select/epoll. Reserve admission before serialization; allocation/error paths release ownership. |
| Stdio | Native byte PIPE input/output through public descriptor try-I/O and `wait_many`; other bindings are refused. An owned stop pipe interrupts readiness even with stdin left open. stdout is LSP framing only. |
| Time | Checked realtime/monotonic clocks and elapsed pacing. No fake PID, OS version, CPU-time accounting or CPU clock. |
| Profile gates | No native watchers, TCP, subprocess, formatter/spelling/name-style features, executable configuration/plugins, downloads/migrations or server-side config writes. Omit their capabilities; excluded module entry points fail explicitly. |

Initial workspace scanning and editor document events remain. External changes
require explicit workspace reload/restart. Real thread/channel concurrency is
retained; timers use their actual next deadline rather than a polling worker.

Program-private provisional admission limits are recorded beside the binding in
`ports/lua-language-server/pyxis/concurrency.md`: 8 workers, 32 channels,
256 items and 64 MiB per channel, 4096 items and 256 MiB overall, 16 MiB encoded
messages, 8 KiB LSP headers and 4 MiB bodies. Serialization reserves scratch and
flattening space before copying. Pending Lua jobs/method queues are bounded too.
These are implementation limits, not ABI contracts or measured memory needs.
Oversized input fails visibly; congestion waits only while consumer/stop can
progress. The first runtime fixture does not qualify large projects.

LSP `shutdown` acknowledges but keeps input live for the later `exit` notification.
Exit signals cancellation, wakes blocked readers/producers, joins workers,
closes Lua FILEs with native finalization rules and then removes instance state.
Actual worker failures retain nonzero status. Cleanup retains the original
state directory capabilities and checks live identity before name-based removal;
the existing REMOVE API supplies no namespace-mutation lease against another
holder of the same writable home. This is not a filesystem sandbox.

## Neovim, authority and package

[Setup and JSON settings](../userland/lua-language-server.md) and the
[task 2 build/QEMU record](../development/experiments/lua-language-server-editor.md)
describe the implemented package/editor slice and its remaining limits.

The opt-in public program package, `pkg install lua-language-server`,
exports `lua-language-server` as an ordinary bundle command. It embeds the
Lua/bee/LPegLabel runtime, licences, provenance and covered port sources, with
scripts/templates/locales in read-only app resources. No shell wrapper, target
compiler/Ninja or automatic download is involved.

The manifest declares memory/manage, threads/manage, clock/read+precise+sleep,
pipe/create for internal wakeups, and random/read for real exclusive state
creation. It requests no launcher, network or SDK root. Like clangd, ordinary
bundle launch also inherits Neovim's exported filesystem roots and cwd with
their existing masks: **home can remain writable; this is not workspace
confinement.** Do not claim a READ declaration attenuates another inherited
alias. App resources and the delegated Neovim runtime remain read-only.
Stricter root attenuation would be a separate generic launch task, not a
call-site authority wrapper.

The editor already exports `nvim_runtime://` for its actual admitted runtime.
The native config sets `workspace.library` to `nvim_runtime://lua`, including
its `vim/_meta` definitions from that selection. No package lookup grant is needed.
The existing editor URI bridge opens definitions through Neovim's own held
read-only runtime root; server-root views are for roots only the server holds.
The current `_meta/api.gen.lua` gives
real callable signatures, not merely an allowed global named `vim`.

The native `lua_ls` configuration is opt-in through
`vim.lsp.enable('lua_ls')`, with `.luarc.json`, `.luarc.jsonc`, `.git` root
markers and an honest single-file fallback. Neovim config uses **Lua 5.1**:
this Neovim embeds PUC Lua 5.1, not LuaJIT. Pyxis Lua scripts/config projects
select their actual Lua version through per-project JSON configuration;
the language being analysed is independent of the server's embedded Lua 5.5.
Neovim's existing LSP client handles semantic tokens, completion and diagnostics.
API types come from actual annotations, not an allowed-global substitute.

Generated metadata and logs use the owned per-instance state below
`home://.cache/lua-language-server/`; read-only templates and the Neovim `_meta`
input are separate. Only JSON/JSONC settings are admitted. Workspace Lua code,
plugins, third-party migrations and configuration-write actions are not exposed.
A forced stop can leave its instance directory for manual cleanup; no persistent
analysis cache or `--lazy` reuse is provided.

## Delivery

1. [x] **Pinned runtime/platform closure.** Ports recipe and coherent embedded
   Lua/native modules; source/licence closure, native filesystem/URI, timing,
   threads/channels and stoppable stdio loop. Add reached standard libc/C++
   functions on real native objects if required. First checks: actual link,
   initialize/shutdown and refused missing authority, with all workers drained.
   **After this task chronium can:** run a real stdio LuaLS process on public fixtures.
2. [x] **Program package and editor integration.** Public program package,
   native `lua_ls`, matching read-only Neovim annotations, JSON configuration,
   state lifecycle and truthful capabilities. Ports/userland first if changed,
   parent pinned to published heads; dependency PRs fast-forward-only.
   **After this task chronium can:** install and use API-aware Lua editing in QEMU.
3. [ ] **Qualification and record.** QEMU first, then an orchestrated vela
   session with chronium. Record artifacts, grants, startup and edit-to-result
   samples, peak backing/thread counts and cleanup. Size the provisional
   message/framing/queue limits from real workload measurements with explicit
   headroom (chronium, 2026-10-25); do not retain 16 MiB messages, 4 MiB LSP
   bodies, 256 items/channel or other provisional values merely because they
   passed small fixtures. Record the observed maxima, corpus and chosen
   headroom. Measure the existing public fixture/editor launch baseline before
   changing those paths; repeat matched runs and keep host/nested/native
   evidence separate.
   **After this task chronium can:** use the qualified Lua LSP natively.

Qualification must prove more than a coloured screenshot:

- Request `vim.api.nvim_buf_` completion and verify Function/Method candidates,
  typed parameters and applied edit/resolve data, including members absent from
  fixture text. Text-only word candidates fail the goal.
- Decode the negotiated semantic-token legend and tuples. Require `vim`'s
  meta-library classification and callable API tokens; upstream need not mark
  every nested field `defaultLibrary`. Toggle rendering above Tree-sitter.
- Introduce/fix a syntax error, undefined non-API name and incorrect typed API
  argument; check diagnostic version/range and visible update. Test hover and
  a definition into the held read-only Neovim annotations secondarily.
- Check URI encoding, relative requires, two projects, unavailable roots,
  single files and external-change/config reload behavior. Check that unsupported
  watcher/network/spawn entry points cannot run; omit formatter capabilities
  and dependent actions rather than returning pretend success. No owner's
  private projects are test inputs.
- Check package absent/required grant absent, cache creation/write failure,
  input/output closure, shutdown+exit with stdin open, editor exit/forced stop,
  and repeated starts without orphan workers or spinning waits.

## Accepted decisions (chronium, 2026-10-25)

1. **First feature profile and refresh.** Native stdio with real,
   owned worker/channel concurrency, bounded framing/queues and cancellable
   input; join workers at orderly exit after the shutdown/exit exchange.
   Semantic tokens, typed completion, diagnostics
   and then navigation. No TCP mode, subprocess tools, debugger, formatter,
   spell/name-style features or native watchers. Initial workspace scan and
   editor didOpen/didChange/didSave/didClose remain; external changes require
   explicit workspace reload/restart. Do not advertise unsupported capabilities.
   Elapsed budgets use real monotonic time, not fabricated CPU accounting.
2. **Authority and configuration.** Ordinary declared-grant bundle
   launch with the same inherited filesystem authority as clangd, including
   possibly writable home/cwd; no new confinement claim or wrapper. Read-only
   app and held `nvim_runtime` library; no launcher/network/SDK grants. Accept
   JSON/JSONC settings, and explicitly reject executable Lua configuration,
   project plugins and third-party code/download/migration paths in this slice.
   Exclude server-side config-write commands/actions; never pretend they saved.
3. **State lifetime.** one real `mkdtemp` instance under
   `home://.cache/lua-language-server/`, owning log/generated-meta subtrees.
   No persistent analysis-cache reuse or `--lazy` initially. Missing writable
   state/entropy/clock fails visibly, with no misleading successful LSP startup.
   Orderly exit closes files and removes only its owned subtree; a crash or
   forced stop may leave that leaf for manual cleanup, never reuse/delete a
   possibly live instance. Random naming prevents collisions, not access.

Source audits, cache receipts and task 1 build/runtime captures are under
`/shared/present/lua-ls/`. No native run, public package upload, compiler-container
change or private-project access is part of task 1.
