# Lua language server

Investigation and proposal (chronium assignment, 2026-10-25). **No port is
implemented or authorised by this document.** The three decisions below are
proposed defaults; stop for chronium before implementation.

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
| bee.lua | `f55b6988e8d9564816a555118b0e76564ef9a283` | MIT; embedded Lua 5.5.0 with its Lua.org/PUC-Rio notice, bundled fmt MIT and retained third-party notices |
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

[make.lua](https://github.com/LuaLS/lua-language-server/blob/b144c872f081373a893b55979142fed651e4425a/make.lua#L1-L64)
selects C11, C++17, embedded Lua 5.5 and static bee/parser registration.
bee's `onelua.c`, its bootstrap and native modules must use one coherent Lua
ABI. Do not substitute the installed Lua command or Neovim's Lua 5.1 runtime.
bee's module hooks also call `dlopen(NULL)`/`dlsym` even with static module
registration; replace those hooks with actual statically linked entry points,
not a pretend dynamic loader.
The upstream Linux source selection has 47 translation units before the
optional formatter's 49; this is source inspection, not a Pyxis compile result.

**Recommend a small ports Makefile running on horse**, using the existing SDK
compiler, runtime link group and explicit source list. Retain upstream code
formatting and source dependencies, replace platform units deliberately and
never select Linux to pass a platform check. Host luamake can express a cross
compiler, but its platform/bootstrap/link selections still need adaptation and
its own distinct bee closure. Direct Make avoids that extra build dependency.
Neither Ninja nor luamake belongs on the target. No source finding requires a
compiler-container rebuild; report a concrete compiler/SDK failure before
requesting one. No build or link has been attempted in this investigation.

## Actual runtime requirements

The default stdio server is not an unthreaded Lua script. It recruits four
shared-process workers: blocking stdin reader, timer work and analysis workers.
Each worker owns a Lua state. Channel serialization and real concurrency stay;
do not replace workers with synchronous success stubs. Bound framing headers,
Content-Length bodies and queued bytes/items; reject malformed, truncated or
oversized messages before unsafe growth. Reserve queue admission before taking
ownership. Congestion may pause a producer only while consumers/cancellation
can progress; failed admission ends the session visibly, never drops a message
or reports completion. Set and record the port's capacities from public fixture
measurements in task 1, not as ABI promises or guessed memory requirements.

| Reached API / call site | Native work and honest limit |
| --- | --- |
| `bee.filesystem`: bootstrap, `main.lua:1`, workspace, loading, metadata and caches | File/directory operations over native capabilities and libc; scheme-aware path joins, roots and classification. bee's default `std::filesystem` backend cannot simply be enabled: the SDK disables that library. Missing identity/time or unsupported operations remain unknown/errors. Standard functions actually needed belong in libc/C++ on native objects; no full filesystem implementation is presumed. |
| `bee.sys`: bootstrap `sys.exe_path`, Windows-only fullpath branches | Use the admitted immutable app root and tracked working path, not `/proc/self/exe`, `readlink` guesses or fabricated host paths. |
| `bee.thread` and `bee.channel`: `script/pub/pub.lua:1-93` | Real SDK threads, serialized queues and native wake pipes/conditions. Upstream channel wakeups use socketpair; replace that implementation, retaining queue ownership and readiness semantics. |
| `bee.select` in pub; `bee.epoll` in `script/brave/brave.lua`; `bee.socket/select` in `service/net.lua` | Native readiness/wakeup integration, not new POSIX select/epoll/socketpair kernel interfaces. Stdio imports the network module unconditionally today; gate that import and socket construction. A reader must wait for stdin or shutdown and be joinable even if stdin remains open. No sleeping poll loop or uninterruptible shutdown. |
| Lua `io` and service/proto input/output | Real FILE/pipe bytes and Content-Length framing; stdout contains LSP only, diagnostics use stderr/logs. Preserve partial reads/writes, EOF, broken peer and cancellation errors. |
| `bee.time`, `bee.thread.sleep`, `os.clock` duration call sites | Check real wall/monotonic clock failures. Adapt elapsed measurements and scheduling to native monotonic time; do not implement `os.clock` or `CLOCK_THREAD_CPUTIME_ID` as fake CPU time. `bee.time.thread` has no server caller. Describe diagnostic rate/cost accounting as elapsed pacing after this adaptation. |
| `bee.subprocess`: `files.lua:17`, `service/service.lua:251-264`, CLI check helpers | No ordinary stdio child process is needed. Remove PID-based cache naming and those unconditional imports; do not invent `get_id`. CLI spawning/check modes outside the profile must refuse explicitly. |
| `bee.filewatch`: `script/filewatch.lua`, workspace watchers | Not present natively. Explicitly disable watcher construction and registration; retain initial scan and editor document events. No inert watcher object or claim of live external-file tracking. |
| `bee.platform`, serialization, crash/debug helpers | Real `pyxis` platform branch, existing architecture and real serialization. Gate unsupported debugger/network/crash-handler paths; Pyxis native fault reporting remains authoritative. No Linux/PID/signal impersonation. |
| `code_format` and dependent diagnostics/actions | Omit formatter code and its runtime advertisements. `pcall(require)` alone is insufficient: formatting capabilities and spell/name-style diagnostics/actions must be gated, so clients never see successful fake support. |

The reached filesystem binding set is `path`, join/string conversion,
`parent_path`, `filename`, `is_relative`, `exists`, `is_directory`, `pairs`,
entry `status:type()`, `absolute`, `symlink_status`, `create_directories` and
`remove`. `copy_file` is conditional; `canonical`, `relative` and `current_path`
are in excluded CLI paths, and no runtime `rename` call was found. Do not promise
all upstream filesystem exports. Threads call `create`, `errlog` and `sleep`;
module initialization also needs a real current thread identity. Channels call
`create/query/push/pop` and obtain wake endpoints. Selectors call `event_add`
and `wait`; workers create/add/wait/close their poller. These endpoint uses must
be adapted together, without pretending native handles are POSIX descriptors.
Platform initialization's unused `os_version`/uname query must be gated or
report a real native value, not silently use a Linux version.

The source paths above are at the pinned
[server tree](https://github.com/LuaLS/lua-language-server/tree/b144c872f081373a893b55979142fed651e4425a)
and [bee tree](https://github.com/actboy168/bee.lua/tree/f55b6988e8d9564816a555118b0e76564ef9a283).
The existing libc has native FILE/descriptors, working path, environment,
directory creation, `mkdtemp`, clocks and threads. It has no getpid,
socketpair/select/poll surface or CPU-clock implementation to assume here.
The embedded Lua's reached standard-library imports need a real link audit;
unavailable process, CPU-time and dynamic-module features are refused or
explicitly removed from this program's profile, never made to succeed.

Path adaptation is mandatory above libc too:
[file-uri.lua](https://github.com/LuaLS/lua-language-server/blob/b144c872f081373a893b55979142fed651e4425a/script/file-uri.lua#L26-L59)
misencodes `home://x`, `files.normalize` collapses `://`, and workspace code
uses POSIX relative-path classification. Use Neovim's existing reversible
`ROOT://rest` ↔ `file:///ROOT/rest` mapping, including encoding and root
boundaries, with scheme-aware lexical operations. Descriptive paths do not
prove identity or create authority.

## Neovim, authority and package

Recommend one opt-in public program package, `pkg install lua-language-server`,
exporting `lua-language-server` as an ordinary bundle command. No shell wrapper,
host compiler, host Ninja or automatic download. Embed the required Lua/bee/
LPegLabel runtime and ship licences, provenance and covered port sources.
Keep scripts/templates/locales in its read-only app resources.

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
Set LuaLS `workspace.library` to `nvim_runtime://lua`, including its `vim/_meta`
definitions, not a later package selection. No package lookup grant is needed.
The existing editor URI bridge opens definitions through Neovim's own held
read-only runtime root; server-root views are for roots only the server holds.
The current `_meta/api.gen.lua` gives
real callable signatures, not merely an allowed global named `vim`.

Add a native `lua_ls` configuration and opt-in
`vim.lsp.enable('lua_ls')`, with `.luarc.json`, `.luarc.jsonc`, `.git` root
markers and an honest single-file fallback. Neovim config uses **Lua 5.1**:
this Neovim embeds PUC Lua 5.1, not LuaJIT. Pyxis Lua scripts/config projects
select their actual Lua version through per-project JSON configuration;
the language being analysed is independent of the server's embedded Lua 5.5.
Enable semantic tokens, existing real LSP completion and diagnostics. Do not
mask missing API definitions with only `diagnostics.globals = { 'vim' }`.

Upstream writes generated built-in metadata, logs and cache state. Merely
setting LOGPATH/METAPATH is insufficient: `script/library.lua:187` writes
`ROOT/log/middleScript.lua`; redirect or remove this diagnostic artifact.
Use real exclusive per-instance state, not a fake PID. Validate that configured
metadata paths cannot redirect generated writes outside that state subtree.
Read-only templates and the Neovim `_meta` input are distinct from generated
LuaLS metadata. Workspace Lua config, plugins and third-party config migration
can execute code; explicit source gates are required for the proposed JSON-only
profile, not just the absence of launcher authority. Exclude server-side
configuration-write commands/actions too: `client.lua:375-444` currently writes
JSON settings and can ignore save failures. Do not offer those actions or
acknowledge them as applied; ordinary editor-owned edits remain available.

LSP `shutdown` acknowledges the request but keeps input alive for the later
`exit` notification. At orderly exit, wake/cancel the reader and workers, drain
their ownership and join before normal process finalization. Do not join a
worker still blocked on stdin, tear down early at `shutdown`, or assume libc
exit runs C++ destructors while sibling threads are still live.

Measured source data: `script/`, English locale, built-in templates and bootstrap/
main/licence files total **1,892,100 bytes**. This excludes the executable,
additional notices and generated metadata. The upstream Linux-x64 release
archive is 3,696,210 bytes; it is not a Pyxis binary or package-size prediction.
Measure the actual P1F, bundle/package sizes, thread stacks and peak backing at
implementation. There is no target size, performance or memory estimate here.

## Proposed delivery

1. [ ] **Pinned runtime/platform closure.** Ports recipe and coherent embedded
   Lua/native modules; source/licence closure, native filesystem/URI, timing,
   threads/channels and stoppable stdio loop. Add reached standard libc/C++
   functions on real native objects if required. First checks: actual link,
   initialize/shutdown and refused missing authority, with all workers drained.
   **After this task chronium can:** run a real stdio LuaLS process on public fixtures.
2. [ ] **Program package and editor integration.** Public program package,
   native `lua_ls`, matching read-only Neovim annotations, JSON configuration,
   state lifecycle and truthful capabilities. Ports/userland first if changed,
   parent pinned to published heads; dependency PRs fast-forward-only.
   **After this task chronium can:** install and use API-aware Lua editing in QEMU.
3. [ ] **Qualification and record.** QEMU first, then an orchestrated vela
   session with chronium. Record artifacts, grants, startup and edit-to-result
   samples, peak backing/thread counts and cleanup. Measure the existing public
   fixture/editor launch baseline before changing those paths; repeat matched
   runs and keep host/nested/native evidence separate.
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

## Decisions requested (defaults, not accepted)

1. **First feature profile and refresh.** Default: native stdio with real,
   owned worker/channel concurrency, bounded framing/queues and cancellable
   input; join workers at orderly exit after the shutdown/exit exchange.
   Semantic tokens, typed completion, diagnostics
   and then navigation. No TCP mode, subprocess tools, debugger, formatter,
   spell/name-style features or native watchers. Initial workspace scan and
   editor didOpen/didChange/didSave/didClose remain; external changes require
   explicit workspace reload/restart. Do not advertise unsupported capabilities.
   Elapsed budgets use real monotonic time, not fabricated CPU accounting.
2. **Authority and configuration.** Default: ordinary declared-grant bundle
   launch with the same inherited filesystem authority as clangd, including
   possibly writable home/cwd; no new confinement claim or wrapper. Read-only
   app and held `nvim_runtime` library; no launcher/network/SDK grants. Accept
   JSON/JSONC settings, and explicitly reject executable Lua configuration,
   project plugins and third-party code/download/migration paths in this slice.
   Exclude server-side config-write commands/actions; never pretend they saved.
3. **State lifetime.** Default: one real `mkdtemp` instance under
   `home://.cache/lua-language-server/`, owning log/generated-meta subtrees.
   No persistent analysis-cache reuse or `--lazy` initially. Missing writable
   state/entropy/clock fails visibly, with no misleading successful LSP startup.
   Orderly exit closes files and removes only its owned subtree; a crash or
   forced stop may leave that leaf for manual cleanup, never reuse/delete a
   possibly live instance. Random naming prevents collisions, not access.

Evidence: source-only audits and cache/release receipts are under
`/shared/present/lua-ls/` (`build-closure-audit.md`, `runtime-api-audit.md`,
`neovim-native-audit.md`). No host/target compile, QEMU/native run, implementation,
package upload, compiler-container change or private-project access was done.
