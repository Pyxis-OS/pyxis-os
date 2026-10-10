# Neovim static parsers (2026-10-10)

[Implemented behavior](../../../userland/neovim.md). Baseline Pyxis `4e7343cd`,
userland `0f9505d9`, ports `7da1269`; implementation uses the same kernel/SDK
and ports `14347d5`. No userland or kernel changes. Existing LLVM 23.1.3 builder
(`pyxis-llvm23.1.3-49e2c1a`); ordinary `make -j16 image` passed. All six grammar
archives matched Neovim 0.12.5's dependency hashes through owner mirrors;
original licences and provenance are staged. No compiler rebuild.

Interactive QEMU 10.2.2, nested KVM, CPU max, four CPUs, 8 GiB, standard VGA
1280×800, OVMF, live RAM home. The local launcher used VirtIO-SCSI optical boot
as in the [first-slice record](../neovim-first-slice/README.md). Baseline and
implementation ran separately with the same configuration; shared-host
variation remains. These results are not native measurements.

| Manual check | Result |
| --- | --- |
| Baseline Lua | Opening a new `.lua` reproduced the ftplugin `Parser could not be created` error. |
| Lua, Markdown, help | Opened without the baseline error; actual `lua`, `markdown` and `vimdoc` parsers and active highlighters observed. |
| Markdown first | Before explicitly loading other grammars, inline content and fenced C created `markdown_inline` and `c` children; an unknown Python fence created no child or error. |
| C and Vim | Kept upstream legacy defaults; explicit Tree-sitter activation parsed and highlighted both. |
| Query and registration | Query ftplugin highlighted a query buffer; all seven built-ins loaded successfully and reported actual ABI 15. |
| Missing parser | Python `language.add()` returned nil and its reason; `start()` returned quietly, kept `syntax=python`, and created no highlighter. |
| Manual syntax | An intentionally empty syntax under `:syntax manual` stayed empty after a missing-parser start. |
| Files and exit | Saved edited files, reopened all four in a fresh editor plus help, and returned to the shell; alternate screen and keyboard input restored. |

Stock environment was also checked: `HOME` was nil and `stdpath("state")`
returned `home://.local/state/nvim`.

Registration, immutable grammar lifetime, explicit-path refusal and deprecated
`require_language()` routing were also inspected. The parser health module now
lists built-ins, but the existing generic `:checkhealth` frontend rejects native
scheme paths before reaching it; that frontend remains follow-up work. No
native run, injected failures or new test infrastructure. Raw captures stay
local under `<worktree>/build/neovim/`.

## Size and startup

P1F size: **5,040,846 → 7,662,686 bytes**, +2,621,840 bytes (52.0%).
Five tab launches per revision used
`boot://share/neovim/nvim.pxb --startuptime home://sampleN.log home://sample.c`,
with an empty, not-yet-created C file. Read the `NVIM STARTED` marker; typing
and firmware boot are excluded. C's default does not activate a parser, so
these samples measure editor startup, not parsing cost on large files.

| Revision | Samples (ms) | Median | Range |
| --- | --- | --- | --- |
| Before | 45.214, 53.115, 54.597, 52.511, 49.649 | 52.511 | 45.214–54.597 |
| After | 58.342, 55.861, 49.002, 52.087, 48.273 | 52.087 | 48.273–58.342 |

The ranges overlap; no startup speedup is established. Native recheck: open a
Lua file, C file, Vimscript, Markdown with inline text/fenced C, and `:help`.
Expect no missing-parser error, the same upstream highlighting defaults, and
normal edit/save/quit behavior. Native milestone closure remains separate.
