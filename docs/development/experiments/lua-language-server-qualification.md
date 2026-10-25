# LuaLS capacity and editor qualification

2026-10-25; QEMU measured qualification and chronium's attended vela HWP PXE
editor check. QEMU results do not qualify native performance or every project size. Raw public fixtures, receipts and captures:
`/shared/present/lua-ls/task3/`.

## Inputs and method

Parent base `4c5ed5f4` (later documentation-only main changes do not affect
the artifacts), ports A `a05ca2c3` / B `fcdf2dd6`, userspace `67aa6220`.
Delivery was rebased after the CA-bundle and libultraship main changes; the
LuaLS recipe subtree is identical. Delivered userland adds `nearbyintf` and its prototype plus the opt-in editor
example setting. This binary does not reference the added function, and existing
reached runtime implementations are unchanged.
LuaLS 3.19.2 with its pinned Bee/Lua 5.5 runtime, ordinary SDK/compiler
LLVM 23.1.3 `3d69e83`. No kernel, libc, SDK or compiler changes.
The SDK was exported at `db52da3e`/userland `b468f20c`; reached libc/lib source
is unchanged apart from that unreferenced addition. See the [task 2 image/package record](lua-language-server-editor.md)
for the explicitly mixed qualification image and its exact package admission.
It retains an older archive editor, so commands select installed
`pkg://nvim/nvim.pxb`; normal baseline server lookup stays bare.

QEMU q35/KVM, CPU max, four vCPUs, 2 GiB, private OVMF vars and modern VirtIO
network; no native device/project or owner configuration. Corpus: selected
public LuaLS `script/` tree, 248 Lua files / 1,665,517 bytes, plus Neovim's held
`nvim_runtime://lua` annotations. Largest project file `parser/compile.lua`: 147,425 B; the held library also
contains 423,854-byte `vim/_meta/vimfn.gen.lua`, accounting for the largest
serialized worker payload.
The manual editor opens four real source files, requests symbols and semantic
tokens, then issues a bounded 64-request symbol burst. Separate functional
fixtures use normal client requests and authored public documents.

Three corpus instances and three functional clients were counted by a private
build: queue counters under existing exclusion, Lua queue/method gauges per
actual Lua VM, exact LSP framing and serialization preflight maxima. One stderr
summary per normal exit was reassembled across 4096-byte Neovim RPC log chunks.
All six summaries validate: five Lua states, four joined workers, zero counter
underflows, no oversized message or admission waits. Instrumentation is retained
with the raw evidence and **is not shipped**. An earlier private counter build
mistook coroutine handles for independent states; its reports are excluded.

## Observed capacity and selected headroom

| Quantity | Initial measured maximum | Selected bound |
| --- | ---: | ---: |
| Conservative encoded message | 423,872 B | 8 MiB (19.8×) |
| Accounted bytes/channel; aggregate | 1,271,633; 2,302,689 B | 32; 64 MiB |
| Accounted items/channel; aggregate | 70; 70 | 512; 1024 (7.3×; 14.6×) |
| Graph tables; visited values; depth | 94; 694; 8 | 8192; 65,536; 32 (generous traversal guards) |
| LSP input; output body | 152,808; 720,906 B | 4 MiB (at least 5.8×) |
| LSP header | 26 B | 8 KiB including optional headers |
| Pending tasks/pool; queued methods; active methods | 215; 70; 79 | 1024; 512; 512 (at least 4.8×) |
| Worker bootstrap | 260 B | 4 KiB (15.8×) |
| Workers; channels | 4; 8 | 8; 32 |

The 8 MiB message bound admits a full-text 4 MiB LSP payload and bounded
metadata. Its 3× serialization reservation must fit an empty channel and total
budget, now checked at compile time. Global admission backpressures a third
maximum reservation. Framing alone does not guarantee graph admission:
independent structure limits still apply. Review raised the initial 512-table /
4096-value limits to 8192 / 65,536: modest-fixture peaks do not bound large
generated files or dense configuration arrays. Keys count as visited values
too. Byte budgets and depth 32 are unchanged. These are tuning choices,
not ABI contracts or guarantees for arbitrary projects/JSON graphs.

A separate debugger-perturbed corpus run sampled the sole five-task server
through four read-only, all-stop kernel inspections. Highest observed backing:
1,143,627,776 B (1.07 GiB), including Lua heaps, code and stacks. This is a
sampled high-water mark, not an exact peak, fault-injection result or leak proof.
Early raw reserved-size totals included free address ranges; corrected summary
excludes those. **Queue admission does not bound whole-process memory.**

## Graph-limit review follow-up

The graph guards were widened to 8192 distinct tables and 65,536 visited values;
depth remains 32. The initial modest workload's 94/694 counts are not universal
large-file maxima. Byte/queue budgets remain unchanged. Structural guards still
bound traversal and scratch independently, so sufficiently dense/deep input can
be refused below the byte ceiling; bytes are not an unconditional admission
promise. The updated ordinary build has no measurement hooks.

Brief requalification used a fresh q35/KVM/four-CPU/4 GiB guest with the same
qualification ISO and normal editor package; this is not a timing comparison.
All inputs were public/authored, with no native/PXE action:

- Generated Lua table: 10,000 entries, 218,945 B; 10,001 document symbols,
  50,015 semantic-token integers and typed hover of the last field passed.
  The client-reencoded symbol result was 2,194,766 B, semantic result 110,042 B;
  these sizes are not an instrumented wire-byte maximum.
- A valid `diagnostics.globals` client configuration array containing 10,000
  names applied in full. Its copied reader-worker graph requires at least
  20,001 table/key/value visits before the protocol envelope. This directly
  covers the broader array/value allowance, whereas raw file text is a string.
- Opened Neovim's largest runtime Lua file, 423,854-byte
  `vim/_meta/vimfn.gen.lua`, and obtained 485 top-level symbols.
- The ordinary semantic/completion/diagnostic/navigation/two-project/URI
  functional pass succeeded again. Normal client exit removed owned state.

Raw result and script: `/shared/present/lua-ls/task3/graph-review/`.
No new counters, benchmark or native run was performed. Chronium's native
feature result below preceded this source-only widening of the graph guards.

## Editor behavior

Negotiated semantic legend/five-tuples decode to `vim` variable/global, `api`
property and `nvim_get_current_buf` method. Upstream marks a global
`defaultLibrary` only when its declaration is in a meta file; Neovim defines
its root in ordinary `_core/shared.lua`. Callable annotation inference remains
real: completion supplies an absent-from-file `nvim_buf_set_lines` Function,
resolves integer parameters and applies the returned insertion through the
editor's edit API. Five hovers carry typed API documentation; definition opens
held `nvim_runtime://lua/vim/_meta/api.gen.lua` read-only. Semantic rendering
stops/restarts above active Tree-sitter (priorities 125/100).

Syntax, undefined-name and wrong-typed API arguments reach the current-version
Neovim diagnostic store at the offending ranges, then clear after correction.
Upstream can first replay cached older diagnostic content tagged with the new
version before replacing it; empty clear notifications omit optional version.
Two roots infer different types from their own relative module, definitions
stay in the right project, and literal space/percent filenames roundtrip.
Normal client stop/editor exit restore the initially empty owned cache.

One unprofiled functional pass measured typed completion 24.886 ms, resolve
40.214 ms, and five hovers median 1.542 ms (0.580–114.652); these are nested
observations with cache/scheduling variation, not native responsiveness claims.
Re-encoded client reply sizes are not substituted for exact wire maxima above.

## Matched timings and native status

Before code, three ordinary package runs measured initialize median 229.418 ms
(204.680–237.105), workspace ready 10,888.003 ms (10,780.469–12,730.824), and
64-request burst 13,897.870 ms (13,267.657–14,180.061). Readiness includes a
500 ms quiet-progress observation. Profiled/debugger timings are not compared.
Matched unprofiled A1/B1/A2/B2/A3/B3 used the same QEMU/kernel, SDK, public
corpus and ordinary bundles under equally backed tmp paths. The final B binary
is 819,290 B versus A's 818,762 B. All six matched runs and the final full
functional pass succeeded.

| Phase | A median [min–max], ms | B median [min–max], ms |
| --- | --- | --- |
| Initialize | 128.015 [117.330–392.992] | 231.950 [114.091–290.529] |
| Workspace ready | 14,259.808 [11,248.991–37,948.098] | 14,970.272 [10,174.637–17,054.401] |
| First symbols | 5479.728 [4004.389–17,187.326] | 6372.317 [4134.825–8836.375] |
| 64-request burst | 15,462.976 [14,454.085–16,242.668] | 13,274.686 [12,556.410–15,897.576] |

Ranges overlap widely; this small nested series with uncontrolled host/cache
variation does not establish a speed change. Neither private instrumentation
nor debugger timing is used in this comparison. Native responsiveness was
judged interactively; no native latency samples were collected.

## Attended native result (chronium, 2026-10-25)

Chronium reports that LuaLS works in his Neovim on vela's HWP PXE image
(archive `82ae36202798`, ABI `0.0.8`; preparation observed HWP applied on all
16 CPUs with no failures):
hover, typed completion with descriptions beside the menu, semantic highlighting
and diagnostics passed. This is his attended report, not an automated native
benchmark. No screenshot was required. The temporary ordinary server bundle
used the editor's held `nvim_runtime://lua` annotations; no development package
was relabelled or installed on the NVMe 0.0.8 system.

Neovim's default completion behavior inserted the first candidate during typing,
making autotrigger unusable. Chronium confirmed this session option fixes it:

```lua
-- Show the menu and documentation without inserting a candidate while typing.
vim.o.completeopt = 'menu,menuone,noinsert,popup'
```

A Tab-to-accept mapping stays in his personal configuration.

He closed Neovim without running `:LuaLSNativeStop`. The orchestrator checked
`home://.cache/lua-language-server/` over Remote and found it empty:
**stopped via editor exit, cleanup verified empty**. They fetched the existing
`tmp://luals-native/result.json` byte-exactly to
`/shared/present/lua-ls/task3/native/results/native-20261025/result.json`.
It reports `native_status=ready`, `types=pass`, `callable_semantic=pass`, and a
public typed hover for `vim.api.nvim_get_current_buf`. It was written by the
readiness command; it has no stop-command result. Cleanup evidence is the
orchestrator's separate Remote observation.

Native allocation-failure/forced-stop cases, native peak memory and systematic
latency remain unqualified. The QEMU behavior and sampled memory figures above
must not be presented as those native measurements.
