# Shell

`make userspace` builds the SDK, shell and core utilities. The default
`DISTRIBUTION=core` archive contains the [rescue set](../../boot/rescue.list)
and installer files. Installed boots use the pool's configured spaces and
installed packages. The rescue entry starts the built-in rescue space.
See [system layout](system-layout.md) and [init](init.md).

`DISTRIBUTION=full` additionally includes the development programs, examples and
live configuration. Its default [session launcher](session-configuration.md)
starts shells in Development and Read-only, sharing `home://`. Boot starts on
Caelum's tab; use Super+Right to select Development. Live `home://` is RAM and
disappears on reboot; installed systems keep it in the pool's `home` volume.

A first walkthrough uses the volatile `tmp://` root, available in rescue too:

```text
ls boot://
echo hello > tmp://hello.txt
cat tmp://hello.txt
mkdir tmp://notes
cd tmp://
cd notes
ls
cd tmp://
cat hello.txt
exit
```

Later examples use writable `home://`; in rescue, use `tmp://` instead.

On ordinary exit, process cleanup logs completion in Caelum and releases the shell's
resources. Its space, tab, framebuffer contents and shared filesystem trees
remain alive. There is no automatic restart or new input consumer in that
space, but tab switching and kernel presentation continue. In the single-CPU
fallback, Caelum logs and shell output share a TTY and can disrupt line editing.

An optional `screen_capture` resource is preserved through session successors,
service launches, foreground and background commands, and every pipeline stage.
It carries CAPTURE alone, with no transport rights, independently of DRAW and
filesystem grants. A shell without it grants none. The
[`screenshot` boot policy](init.md#boot-configuration) controls which spaces
receive it; [screenshot PATH](screenshot.md) saves a PNG through that authority.

## Commands and quoting

The shell uses libterm's line editor and waits for the complete foreground
command or pipeline. An interactive shell launched with `--no-echo` uses the
quiet editor instead: no prompt, input echo, redraw, cursor or style control,
submission newline or `^C` is written, while editing, cancellation, EOF and the
prompt-derived line limit are unchanged. Application output and shell
diagnostics are unaffected, and the option is never passed to children or
session successors. Other interactive-shell arguments are rejected; the remote
server selects `--no-echo` for machine clients that request it. The prompt shows the working path, for example
`tmp://notes> `. Long paths show an ellipsis and their tail, keeping at least half the first row for
input; control and non-ASCII bytes display as `?`. Whitespace separates
arguments. Single and double quotes preserve whitespace and allow empty
arguments; adjacent quoted/unquoted
pieces form one argument. Outside single quotes, backslash takes the following
character literally, including inside double quotes. Inside single quotes,
backslash is an ordinary character. For example:

```text
mkdir 'two words'
cd two\ words
ls
cd boot://share
cat "hello.txt"
```

Unfinished quotes or escapes report an error without executing anything. An empty
line does nothing. Ctrl+C at the prompt cancels the current line; while a
foreground command runs it [interrupts the command](#interrupting-foreground-commands). Input
loss discards the line; a submitted line that hit the editor's buffer/display
limit is also rejected. The command buffer has room for 1023 bytes plus NUL;
libterm's visible-area limit may be smaller.

Up and Down recall earlier lines into the editor, where they can be edited
before Enter runs them; Down past the newest line brings back what was being
typed. Each interactive shell keeps its own last 100 submitted lines in memory,
leaving out empty and all-space lines and a repeat of the line before. A
`--no-echo` shell and scripts keep none.

**Saved history.** Lines are saved in `home://.history`, one file per home,
shared by every shell of every space that can write that home.
- **Loading:** a shell reads `.history` backwards from its end, in 4 KiB steps,
  until it has the newest 100 valid entries, so it reads a few kilobytes
  however long the file is. It doesn't see lines other running shells save
  later; a new shell, including a `session` successor, picks them up.
- **Saving:** each line the shell records is saved before it runs, as one
  `FILE_APPEND` of the line and its LF to `.history` (created when missing),
  followed by a sync of the file. Only the file's last byte is read, to see
  whether the previous save was torn; nothing is trimmed or replaced.
- **Concurrent shells:** an append picks the end of the file and writes the
  line in one mutation of that file, and a line is far below the 4080 bytes one
  append carries. Lines from different shells therefore land whole and in the
  order the appends complete; none is lost, overwritten or split by another.
- **Crashes and full volumes:** a crash or kill loses at most the line being
  saved. A short append (a full volume) is retried once for the rest of the
  line, then reported as a failed save. Either can leave the file ending inside
  a line. Loading ignores such an unterminated last line, and every save reads
  the file's last byte and starts with an LF when it is not one, so entries
  never merge. The torn line stays in the file, terminated, as an entry of its
  own: Up can recall it, cut short.
- **Private lines:** a line starting with a space stays in memory, so Up still
  recalls it, but is never saved.

**File format.** One entry per line, ending in LF, printable ASCII and at most
1023 bytes.
- **Bounds:** none. The file only grows; delete it, or edit it while no shell
  is saving, to shorten it. Only the newest 100 entries are loaded into memory.
- **Loading:** it skips anything else and leaves it in the file.

**No writable home.** A space without `home://`, or with a read-only one such
as Read-only's, still loads what it can read but saves nothing, without a
message. The first failed save prints one `shell: history not saved` line, and
that shell stops saving.

**Reverse history search.** Ctrl+R opens `(reverse-i-search)` with a separate
query and the current line. Typing finds the newest entry containing that
case-sensitive ASCII substring, with the matched text highlighted. Ctrl+R again
steps to older matching entries. Backspace shortens the query and searches from
the newest entry again.

- Enter runs the shown line. Esc accepts it for editing without running it;
  Left/Right and Ctrl+A/Ctrl+E also accept it, then move the cursor.
- Ctrl+G or Ctrl+C cancels search and restores the original line and cursor.
  Ctrl+C outside search still cancels the line.
- A failed query or exhausted older matches shows `failed reverse-i-search`
  and red text, retaining the last successful entry (or the original line if
  none matched). Enter or Esc still accepts the shown line. The red cue stays
  visible when a narrow pane crops the label.
- Clipboard paste accepts the shown line for editing, then inserts literal text
  under the usual safe-paste rules; it never runs the line automatically.

Search uses this shell's in-memory history, including loaded saved entries and
private lines; it does not reread the history file. The query has the line's
1023-byte bound. Framebuffer tabs, mux panes and remote shells use the same
editor; `--no-echo` and script mode have no history search.

**Tab completion.** Tab completes the word before the cursor, read with the
parser's quoting rules.

- **Command names:** the first word of the line or pipeline stage after `|`,
  typed bare. Candidates are builtins, interactive aliases and names offered by
  the held command-list entries, sorted without duplicates. Conventional entries offer `NAME.pxe`
  files and complete `NAME.pxb` directories as `NAME`; package entries offer
  indexed logical names. Names needing quotes are not offered. Completion is a
  hint: launch performs its own retained lookup and reports selected errors.
- **Paths:** an explicitly path-shaped command word (`./`, `../`, a nested
  relative path or `root://...`), or any other word including a redirection target. Everything up to the
  last `/` is the directory, opened as `cd` and `ls` open one: relative to the
  working directory, or by a `scheme://` root, with the shell's own grants. A
  directory it cannot open offers nothing. Files and directories are offered,
  whatever the command. A directory ends in `/` and a lone one gets no space, so
  the next Tab goes deeper. Names that start with `.` are offered only after a
  typed `.`. A word that begins with a partial root name, such as `ho` or `home:`,
  also offers the caller's roots as `name://`, taken from the shell's own root
  bindings.
- **Quoting follows how the word began.** Unquoted, the inserted text escapes a
  space, `'`, `"`, `\`, `|`, `<`, `>`, `&` and `*` with a backslash (`two\ words`). Inside
  quotes the name is inserted raw, with `"` and `\` escaped in double quotes, and a
  lone file match closes the quote. A name holding `'` is not offered inside single
  quotes. A word that mixes quoted and unquoted pieces is written again in the
  quote state at the cursor.
- **Not offered:** names with non-ASCII or control bytes, `http://` and
  `https://` words (Tab never starts a network request), and the contents of a
  directory with more than 4096 entries.

Matches behave the same for both kinds:

- **One match** replaces the word and adds a space, or nothing after a directory.
- **Several matches** extend the word to their common prefix when that is longer.
  Otherwise Tab lists them in columns under the line and draws the prompt and
  line again. Tab on an empty command position lists every command, and on an
  empty argument position the working directory and the roots.
- **Nothing** happens after a background `&`, for a bare command name that is
  quoted or holds a byte a bare name cannot hold, and when nothing matches.
  Explicit command paths use the same quoting rules as argument paths.
- A completion that would exceed the line limit changes nothing and marks the
  line as full. The shell lists `bin://` and `boot://` and path directories with
  its own grants; what it cannot list it leaves out.

The local shell, a [multiplexer](multiplexer.md) pane and the
[remote terminal](remote-terminal.md) shell complete the same way; the quiet
(`--no-echo`) editor and script mode do not. The editor interface is
[`term_read_line_completing`](terminal.md#tab-completion).

TEXT and capability expansion, command-output capture and `;`/`&&`/`||` use the
[typed-value rules](#typed-values-and-command-sequences) below; there is no word
splitting, and the only [wildcards](#wildcards) are `*` typed in the
command line itself. Unquoted `<`, `>` and `2>` select redirection; `|` connects
foreground external commands. Unsupported operator combinations are errors.
Interactive input treats `#` literally; scripts support whole-line comments.

`cd path` changes the shell's owned directory chain. It requires one argument;
failure preserves the old chain, its rights and the displayed path. `exit [N]`
ends the shell with the selected signed 32-bit decimal status; omitted N is zero.
There is no modulo-256 conversion. Invalid input reports builtin failure without
exiting an interactive shell. Ordinary interactive failures do not accumulate
into its exit status. Mandatory cleanup failure changes requested zero to failure
and preserves a requested nonzero status while reporting the error.

With an optional `terminal_events` grant, the interactive root shell reports one
ordered, typed completion for each nonblank submitted line. Foreground external
work reports after the existing waits: the last pipeline stage's exact exit code,
fault or termination. External preparation or launch failure, including a
missing image or unopenable redirect, reports launch failure. A successful
background command reports launch, not its later exit. Recognized builtins,
including invalid builtin arguments, report builtin success (0) or failure (1);
`session` and `service` remain builtin transactions even though they launch
processes, and `exit` or a successful `session` handoff reports success before
the shell leaves (after the successor wait in a remote handoff). The successor's
outcome is propagated to shell exit, not the builtin status. Parse errors,
command-form errors such as a builtin in a
pipeline or with redirection, and submitted line-limit rejection report
rejection. Diagnostics are written before the completion. This outcome is
reporting only: script stopping, `exit` and fatal handling are unchanged.
Blank lines below the line limit, cancellation, lost input and EOF without
submission emit nothing; submitted line-limit rejection takes precedence.
Fatal command handling emits nothing; failed event emission ends the shell with
failure and is never retried. The event grant remains local to this interactive
shell and is never forwarded to children, scripts or session successors.
See the [remote interface](remote-terminal.md#client-modes) for framing and
persistent machine-client use.

`title [--optional] name` sets the current space's tab label; quote names with
spaces, for example `title "Source editing"`. It accepts 1–63 printable ASCII
characters. An error leaves the old title unchanged. `--optional` only
tolerates a missing title grant. Titles survive shell exit, and only session
handoff inherits the grant by default. See [space titles](init.md#space-titles).
Like the other builtins, `title` cannot run with `&`.

`affinity LIST` narrows the CPUs the current space's tasks may use, for example
`affinity 2-3` or `affinity 1,3`. It needs the affinity grant that only a
space's trusted init receives, and it works only before the space's first
launch. See [affinity setup](init.md#affinity-setup). Indices above 8191 are a
usage error. Like the other builtins, `affinity` cannot run with `&`.

A command without `/` or `:` is a bare name. It resolves through the exported
`path` capability list only. Trusted defaults are read-only bin, distinct
archive/rescue and the live package-command view. An absent/empty list means no
bare search even if boot/bin roots are held. Home, cwd and the old development
catalog are not searched implicitly. Configure session.lua's ordered path or use
`export path=(home://tools $path)` to add held locations; first matching entry wins. Selected errors
refuse rather than falling through. `which NAME` shows that resolver's selected
entry/target and package/version/set, without executing.

Use `boot://cat.pxe`, `./cat.pxe` or an explicit `.pxb` path for path lookup.
A bare name including `.pxe` remains a logical name; no cwd exception is added.
`bin://NAME` no longer adds suffixes or consults a catalog; on an installed
system `bin://` is the package command view, whose entries are command names. A textual PATH does
not alter the native list. PATH-aware port launch accepts absent PATH or its
exact diagnostic-label projection; unsupported values refuse. Builtins are recognized after quote
removal. An empty command name is an error. Opened programs use the
[script-launch helper](script-launch.md), which can dispatch a shebang to a native
interpreter.

[`ls [-1] [-l] [--] [directory...]`](ls.md) sorts directory names by byte order.
Console output uses colored columns fitted to its width; pipes and files keep
plain one-name-per-line output. `-1` forces lines, and `-l` shows kind, byte size
and name with no time column. Directory sizes are `-`; unavailable file sizes
are `?`, with diagnostics and failure status. Unknown options are usage errors.

`echo [-n] [ARG...]` is a native external program. It prints arguments separated
by one space, followed by a newline. An exact first argument `-n` suppresses the
newline; later `-n` arguments and all other option-like text are literal. It
does no escape processing: shell quoting determines the argument bytes. With no
arguments it prints a newline; `echo -n` writes nothing. Output errors report
failure. Redirects and pipelines work as for other external commands:

```text
echo hi > home://note.txt
echo a b | cat
echo -n x
```

[`rm [-r] [--] path...`](rm.md) removes files, and with `-r` directory trees;
`rmdir path...` removes empty directories only. Both accept multiple paths, continue
after an individual failure, and return failure if any removal failed. `rmdir`
has no options or parent pruning. The shell expands [wildcards](#wildcards)
before either runs. Existing handles survive removal, and
removing an empty directory makes it unavailable for new children even through
an older handle. See [the directory contract](../interfaces/directories.md#removal).

`mv [--] source... destination` renames files and RAM/npfs directories with
libc `rename`: to the destination path, replacing a same-kind file or empty
directory, or, when the destination is a
directory, into it under each source's last name. Several sources need a
destination directory. Directory moves preserve the subtree; cross-filesystem
copying is unsupported. It continues after a failed source and reports failure without
deleting the source or destination itself. See [mv](mv.md).

[`cp [-r] [--] source... destination`](cp.md) copies files, and with `-r` directory trees, within or across roots.
A directory destination receives each source's basename; several sources require
an existing directory. Existing files are replaced through a completed sibling
temporary file, so the destination directory needs create, file-write and remove
rights. There is no direct-truncation fallback. Read failures
preserve the old destination; unconfirmed publication reports possible names
without retrying or deleting them. See the reference for tree-copy, concurrency and cleanup
limits.

`sync path...` requests synchronization for each explicit file or directory
path, in argument order. It continues after an individual error and exits with
failure if any target failed; with no arguments it prints usage and fails.
Success is quiet. For example, after an editor closes a saved file, run
`sync host://work/hello.c host://work` to request file and parent-directory
synchronization. The command resolves each path when invoked, so a concurrent
rename can make it sync a different object. Native callers that must sync the
same object they already hold can use libpyxis `file_sync(handle)` or
`directory_sync(handle)`. Syncing one target does not sync the whole filesystem.
See [the host synchronization contract](../devices/virtio-fs.md#synchronization) for
permissions, errors and durability limits.

## Wildcards

An unquoted, unescaped `*` in a command argument is a pattern. The shell replaces
that word with the names it matches, sorted by byte order, each as its own
argument, before the command runs. `rm`, `cp` and `mv` take several paths, so they
need no change.

```text
rm home://*.txt
cp boot://share/installer/system/* system://config/
echo notes/draft-*.txt
rm 'a*b'
```

`*` matches any run of characters in a name, including none. `?` and `[...]` sets
are not patterns, so URLs with query strings, bracketed query arrays or IPv6
literals, such as `http://host/page?id=3`, are ordinary paths. Sets may return
later.

- **Names.** Matching covers the whole name and is case-sensitive.
- **One level.** A wildcard may appear only in the last path component, so
  `home://*.txt` lists `home://` and `notes/*.txt` lists `notes`. An unquoted `*`
  in the scheme or an earlier component is an error; quote it to pass it on.
  There is no recursion; use `rm -r`. Files and directories match. The result
  words keep the typed directory text, and a name with spaces stays one argument.
- **No match.** The command does not run and the shell prints
  `No match: PATTERN (quote it to pass it literally)`. In a script this fails the
  command like any expansion error.
- **Hidden names.** A name starting with `.` matches only a pattern that starts
  with `.`, so `*` skips `.history` and `.h*` matches it.
- **Quoting.** `'*'`, `"*"` and `\*` are literal.
- **Variables.** The text of a variable, positional argument or `"$@"` is never a
  pattern, but a pattern can follow it: `$dir/*.txt` lists the directory in
  `$dir`. A word cannot mix a wildcard and a command substitution; quote one.
- **Names that look like options.** As in Unix shells, a file named `-r` that a
  pattern matches reaches the command as an option. Put `--` before the pattern or
  write `./*` to avoid it.
- **Other words.** The command word and a redirection target are not expanded, and
  an unquoted wildcard in them is an error. A `name=value` assignment keeps its
  text literal, and so does a `name=value` argument such as `cmd key=*.txt`.
- **Authority and limits.** The shell lists the directory with its own grants, as
  Tab completion does, and needs the ENUMERATE right; the command then resolves
  each name with its own authority. There is no fixed count of matches: the list
  is sized from the directory, and the expansion fails with `Out of memory
  expanding PATTERN` before anything runs if memory runs out. A directory that
  changes while it is listed is read again a few times, then the expansion fails.
  The launcher's own argument limit still applies to the final command.

## Typed values and command sequences

`name=value` writes one private TEXT or CAP entry in the native environment;
`export name` or `export name=value` selects it for child inheritance. `unset name`
removes a publication without closing independently held copies. There is no
second shell value table. For example:

```text
title="two words"; echo "$title"
listing=$home; ls "$listing"
export documents=$home
unset documents
```

`$name` accepts letters/underscore followed by letters/digits/underscore;
`${full.name}` accepts the native name vocabulary, including dots, plus and hyphen.
Single quotes and escaped dollars are literal. TEXT concatenates without field
splitting, globbing or reparsing; a semicolon inside a value stays data. A CAP
must occupy the whole word, quoted or unquoted. The child receives its captured
entry under the same name and argv contains `name://`; private operands go only
to the stage using them. A mixed word such as `"prefix:$listing"` is rejected.
Assignment is standalone, not a command-prefix override.

Runtime dependencies and bundle-owned bindings keep their authority. A different
CAP selected under an automatically supplied name is refused before redirect
OPEN or launch, with a hint to copy it to another name. There is no override or
implicit rename. Known common-source selections can narrow, never union rights.
Explicit exported FILE/controller values also survive supported child adapters;
other native resources retain each profile's existing admission rules.

Pipelines bind tighter than `&&` and `||`, which have equal precedence and
associate left to right; `;` separates conditional lists. `&&` runs the next
command on success, `||` on ordinary failure. Syntax is checked for the whole
line before effects, but only selected commands expand: skipped arms perform no
substitutions, CAP lookups, redirects or launches. Quoted operators and operator
bytes in TEXT remain data.

Scripts and `-c` stop after an unsuccessful whole list, before the next `;` or
line. Interactive `;` continues after ordinary failures. Thus `fail || recover;
next` continues if recovery succeeds, while `fail && skipped; next` stops a
script. Conditions retain the existing pipeline result policy, with no `pipefail`:
last user stage ordinarily, every stage in captures. A reported fault aborts the
current list; interruption, fatal wait/cleanup and explicit `exit` stop execution.
Conditional lists are foreground-only; existing builtin/capture restrictions
remain. For example:

```text
./build.pxs && ./app.pxe
./build.pxs || echo "build failed"
shell -c "exit 7"
```

Scripts can expand their own arguments; see [script arguments](#script-arguments). `describe PATH` opens a typed read resource;
`describe $value` describes the held CAP without consuming it or calling its
provider. It prints attested interface/masks, declared fields and unknown length
honestly. Failed outcomes report failure; bounded error bodies are not dumped.
`revoke ${name.control}` requires the explicitly held CUT controller.

### Directory list values

A parenthesized whole assignment value creates an immutable directory CAP list:

```text
saved=$path
path=(home://tools $path)
export path=(home://project/bin $saved)
export path=()
```

The value is a CAP, not TEXT. Members use the ordinary scalar word/quoting rules:
path strings open native directories through held cwd/roots; directory CAPs
contribute their held read rights; list CAPs splice their entries in order.
There is no implicit location or textual PATH translation. List construction
preserves order and owns the selected directory grants before publication.
Failure preserves the old environment entry. Native constructor capacity applies;
there is no separate shell location quota. Empty lists deliberately disable
bare-name lookup; explicit paths and builtins remain available.

Lists occupy an entire named assignment/export value. They do not introduce
command grouping or shell array variables, and member assignments/management
captures are refused. Ordinary captured FILEs are not directories. Members are
not wildcard-expanded. The lowercase `path` entry accepts only a valid list CAP;
`path="home://tools"` refuses instead of replacing it with TEXT. Private assignment
and exported inheritance retain their ordinary meanings. `which NAME` and Tab
use the newly published list, while already-running children retain theirs.
Persistent startup skips are printed by `which`; runtime replacement clears
that diagnostic receipt, leaving the selected list as the lookup authority.

### Held output captures

```text
x=$(echo hello); cat $x
copy=$x; unset x; cat $copy
cat $(echo inline)
files=$(ls home://); cat $files
svc=$(service start boot://textfs.pxe --welcome hello)
export svc; cat svc://welcome
mic=$(revocable $home)
revoke ${mic.control}
```

`$(...)` produces a held CAP, never reply text in argv. Inline results are
command-local entries, released after child capture; nested substitutions obey
the same rule. Sequential byte producers append to one retained result. Capture
uses an owned temporary execution subgroup, checks every pipeline stage, and
waits for subtree retirement before sealing/collecting output. Ctrl+C stops and
drains that subtree; a failed inline substitution prevents the outer launch.
Ordinary builtins retain their existing no-redirection/pipeline rule; the paired
management forms above are explicit exceptions, requiring a named destination.

A fresh, single-source `cat` can hand off a re-readable FILE or held failed value
without copying its bytes or creating another retention charge. One-pass and
byte-only output is retained under the session account: defaults are 64 MiB per
value and 512 MiB per session, configured by the trusted session. Exactly at a
limit succeeds; crossing either produces only an immutable TOO_LARGE failed
FILE, with no readable prefix. Copy/export shares backing and its original
charge until the final holder releases it. Re-readable handoffs have no byte cap.

A named capture publishes its actual evaluated result even on command failure,
with nonzero status. An attempted launch/OPEN without output produces a failed
FILE. An unhandled byte-producing failure publishes a failed value rather than
a successful prefix; a handed-off value retains its own outcome even when its
producer exits nonzero. `$(fail || echo recovered)` can recover an ordinary
command failure, but never clears a failed resource, mutation failure, overflow
or uncertain transaction. Preparation/admission or atomic publication failure preserves the old binding;
earlier external effects are not rolled back. No failed submission is replayed.

Paired management publishes `<name>.control` privately in the same SET as the
primary; optional handed-out observation uses a separate private `<name>.status`.
Companion collisions refuse without replacing old entries. Primary copy/export/
unset never implicitly copies/exports/unsets its companions, and matching suffixes
prove no relationship. A directly collected inline observation can be explicitly
staged with its primary; named redirection never searches for a status companion.

Paired service capture is **leaf-only**: the complete prepared request may contain
no direct LAUNCHER grant, including aliases, unnamed and bundle-selected grants.
Refusal precedes redirects/provider startup; declared grants are never silently
dropped. Publication failure terminates and waits for the leaf provider; success
releases its observer. Use ordinary named bootstrap service startup for services
that spawn children. See the [limitation and revisit point](https://git.internal/PyxisOS/pyxis-os/issues/1489).

Pipes between foreground pipeline stages carry file values (see
[foreground pipelines](#foreground-pipelines)); other capability transport
remains separate C9 work. General
`env copy`/`env unset` are replaced; only the optional bootstrap alias helper
remains for trusted startup. Ordinary named bootstrap services retain their
existing behavior.

## Service publication

Init scripts publish services into the process
[environment](../interfaces/environment.md#publication):

```text
service start counter pkg://counter/counter.pxe --provide
counter --lookup counter
unset counter
```

`service start NAME IMAGE [ARG...]` atomically publishes an exported SLOT at
`NAME` and its private controller at `NAME.control`. A collision on either name
refuses publication. `service replace NAME IMAGE [ARG...]` retargets the held
slot after the new provider transfers its client through IPC. Replacement first
captures both CAP entries from one snapshot and verifies that the native
RETARGET controller belongs to that exact slot. A mismatched controller or TEXT
holder refuses before provider launch; type/rights or a shared current provider
do not prove a pair. The captured pair stays owned through startup, so publication
does not look up a different controller by name. Existing slot
holders follow the new target; already-opened child objects keep their identity.
`unset NAME` removes the binding, without cutting independently held grants.
Controllers are unexported and explicitly delegated only to trusted successors
or supervisors. Ordinary launches receive exported entries selected by their
launch profile, with the original rights or deliberate attenuation. Provider
startup receives declared dependencies and omits its own publication names.
FILE providers use the [shared file bridge](../interfaces/file-providers.md).

A service's stdout and stderr go to the [system log](../interfaces/kernel-log.md#system-log-writes)
as the shell's space (info and warn); `service start|replace --terminal` keeps the
space console instead.

`service start --optional` continues after a provider explicitly reports setup
failure; malformed handshakes, launch failures, environment publication errors and cleanup
failures are still errors. `service start|replace --read-only` restricts the
launched provider's native directory roots and working directories to traversal
and file reads. HTTPS boot startup uses both options; custom trust is selected
with `httpfs --https --ca-bundle URI`. See [HTTPS startup](http-fetch.md).

## File redirection and stdin

Foreground external commands accept `< file`, `> file`, `>> file`, `2> file` and `2>> file`. For example:

```text
echo hello > home://copy.txt
cat < home://copy.txt
cat missing 2> home://errors.txt
cat home://errors.txt
mkdir notes
cd notes
cat < ../copy.txt > "another copy.txt"
```

Redirects follow the command name and may appear between arguments. Whitespace
around operators is optional: `cat<copy.txt>another.txt` works. A redirect consumes
one nonempty filename word using the ordinary quotes/escapes; quoted or escaped
operators are literal. `2>` selects stderr only when the unquoted `2` begins a
word and immediately precedes `>`: `cat 2 > out` instead passes a file operand
`2` and redirects stdout. Other descriptor prefixes, missing filenames, duplicate
redirects for one stream and leading/standalone redirects are rejected.

`<<`, `<>` and other unsupported operator combinations are errors. Builtins (including `session`) and background commands reject redirects.
The whole command is parsed and these restrictions checked before opening files.
Interactive commands and scripts use the same redirection rules.

TEXT paths use the shell's current directory and typed OPEN. A whole-word CAP
redirect opens that held grant directly, without a second name lookup. FILE
results use ordinary file I/O; transaction stdout uses the bounded staging worker
and [commit/capture rules](#prepared-http-requests). No URI spelling selects a
private HTTP route. The shell first opens the executable,
then opens all redirect targets in written order with the current root grants. Input must exist;
output is opened or created without truncation. After wiring pipes, the shell
compares every explicit output with every stage's final FILE stdin, including
inherited input. Different known domains prove distinctness even without object
IDs; within one domain both IDs must be available and different. An alias,
unknown domain, missing required identity or failed query aborts before any
truncation. See [file metadata](../interfaces/file-metadata.md).
Only after this check are non-append outputs truncated, in written order, and
children launched.
Append retains existing bytes and uses [native FILE_APPEND](stdio.md), atomic
per operation of at most 4,080 bytes. Larger flushes/lines may interleave, without
a durability guarantee. Every append-capable redirected FILE, truncated or not,
is passed with `stdout.append` or `stderr.append`; descendants inherit the policy
and continue at EOF. Scripts therefore preserve successive commands' output.

An unsupported backing such as HOST refuses `>>` before create/truncate. Plain
`> host://file` prepares the existing positioned byte relay first; descendants
receive its PIPE and share that worker's cursor. It needs declared helper/pipe/
launch grants and cannot seek or hand off capabilities. Native/capture output
keeps a real FILE. Classification uses held FILE/parent capabilities, not URI
spelling, and a later creation or authority failure is final. Temporary handles
close after launch or failure. `<` withholds keyboard and pointer grants.
If stdout remains a console, the final foreground stage receives named terminal
input with READ alone, so a pager can read keys independently of its file stdin.
The separate terminal-output grant remains available for explicit terminal
operations. Unredirected standard streams retain their inherited bindings.

A syntax error or missing executable path does not touch redirect targets. A later
open failure can leave newly created files, but existing outputs have not yet been
truncated. Once truncation starts, a resize error, malformed executable, missing
shebang interpreter, allocation or launch failure may leave outputs truncated.
There is no rollback or atomic multi-file update. Shell preparation errors use the
shell's own stderr; `2>` redirects the child's stderr only.

`cat < file > alias` is protected even through different root/path spellings.
Argument files are outside the check: `cat file > file` can still destroy its input.
With independent `> out 2> out`, the streams have independent bindings and no
combined ordering. Native append operations cannot overwrite one another, but
independent HOST workers have separate positioned cursors and may overwrite.
Use `> out 2>&1` for one shared byte stream.

### Merged output

Exact `2>&1` duplicates stdout's current sink onto stderr, after pipeline
defaults and in written redirect order:

```text
cmd > file 2>&1
cmd 2>&1 > file
cmd 2>&1 | cat
cmd 2>&1 > file | cat
```

The first combines both streams in the file; the second leaves stderr on the
prior stdout. The third combines both in the pipe; the fourth puts only stderr
in the pipe. PIPE and CONSOLE sinks share the same endpoint and keep ordinary
command wait, cancellation and launcher behavior. A FILE or capture sink uses one bounded
byte relay with a sole cursor, including across successive descendant commands.
Explicit/inherited native append is honored per bounded operation, without
whole-line ordering beyond actual writes and application buffering. Independent `> file 2> file`
redirects still have independent cursors; use `> file 2>&1` to merge.

Merged output is byte-oriented: no seek or capability handoff across the merge
pipe. `cat` uses its ordinary byte-copy fallback; unmerged captures keep their
typed-value behavior. The transaction staging worker is reused for a merged
transaction body. A captured reply cannot share its collector with stderr bytes:
`$(cmd 2>&1 > $api)` is refused before OPEN when `$api` is a provider or
transaction. Use `$(cmd > $api 2>&1)` to merge both streams into the request
body. A collector already holding bytes or a value cannot receive a transaction
reply too. Each extra relay counts against the eight-process batch
limit, checked before outer redirect OPEN/truncation. Multiple sinks can need
multiple helpers. Helpers are not user pipeline stages: ordinary status comes
from the last user stage, and a helper failure also fails the command. An
unexpected helper exit, fault or termination reports command exit 1; it does
not turn an otherwise exited user command into a fault or interruption. Capture
and transaction producers retain their all-stage checks. Descendants holding
writers can delay EOF; cancellation drains the owned subgroup. Preparation
diagnostics remain on the shell's own stderr.

`cat` with no operands reads stdin to EOF. A `-` operand reads stdin among ordinary
file operands; repeated `-` continues the same stream without closing or rewinding
it. Use `./-` for a file literally named `-`. Cat uses `fread_some` for every input:
terminal and pipe data is copied as it becomes available, without waiting to fill
its transfer buffer. File operands and file-backed stdin keep bulk reads and
normal EOF. Terminal input remains raw and blocking. Remote machine-client stdin
EOF sends END_INPUT, which drains queued terminal bytes before EOF; the local
framebuffer console has no input EOF operation. Ctrl+D is an input byte rather
than a stream closure. Ctrl+C at the prompt cancels shell editing; during a
running cat it terminates cat. No options
or terminal line discipline are added.

## Bounded input with head

```text
head [-n N | -c N] [file|-]
```

Head copies the first ten lines by default. `-n N` selects a line count; `-c N`
selects a byte count. An omitted input or `-` reads stdin; otherwise it opens one
file through the ordinary path grants. Options precede the file operand. Use
`--` before a filename beginning with `-`; `./-` names a literal dash file.

Counts contain decimal digits only, from zero through UINT64_MAX. Signs,
suffixes, attached option values, repeated/conflicting count options and multiple
input files are rejected before opening input. A zero count performs no reads or
stdout writes; an explicit file is still opened, so invalid paths fail normally.
Open/close errors still fail, including closing an unavailable standard binding.

Line mode counts newline bytes and preserves all bytes, including CR and NUL.
A final unterminated line is copied unchanged; EOF before the requested count is
successful. Input closes at the limit, EOF or an I/O error, before a final output
write or error diagnostic can block. Errors in
reading, writing or closing report to stderr and return failure.

Head never consumes past its selected boundary. Line mode reads one byte at a
time, at the cost of more native calls for long lines; byte mode uses
`fread_some` with at most 4 KiB and never more than its remaining count. Neither
mode dispatches on the input backend. Line output is staged until a newline,
EOF/error or 4 KiB; byte output forwards each available read.

For example:

```text
echo hello | head -n 2
cat boot://ls.pxe | head -c 16 > home://prefix
head -c 0 home://copy.txt
```

Closing the reader early discards unread pipe bytes and makes further upstream
writes fail with EPIPE. The shell reports earlier-stage failures while retaining
the last stage's result. There are no multiple-file headers or additional head
options. Remote terminal END_INPUT can end either mode before its requested limit;
local framebuffer input needs the requested newlines/bytes or an input error.
Ctrl+D does not close either input stream.

## Foreground pipelines

Connect two through eight external commands with `|`:

```text
echo hello | cat > home://copy.txt
cat < home://copy.txt | cat | cat
cat missing 2> home://errors.txt | cat > home://empty.txt
```

Whitespace around `|` is optional. Quotes and escapes keep it literal. Every
stage needs a nonempty command name; leading, trailing or adjacent pipes are
errors. `|&`, background pipelines and builtin stages (`cd`, `exit`, `mount`,
`title`, `session`, `poweroff`, `reboot`) are rejected before any file is opened. Redirections retain
their ordinary per-stage syntax and duplicate-stream checks. Interactive input
and script lines use the same rules and their existing total line-length limits.

By default stdout flows to the next stage's stdin. The first stage inherits the
shell's stdin, the last inherits its stdout, and each stage inherits stderr
separately. Explicit redirects override that stage's defaults:

- `cat source > saved | cat` writes `saved`; the second cat receives EOF.
- `cat source | cat < other` reads `other`; the first cat has no pipe reader and
  gets EPIPE if it writes.
- `cat missing 2> errors | cat` writes child errors to `errors`, leaving the
  pipeline's byte stream independent of stderr.

The pipes between stages are created with `PIPE_CREATE_VALUES`, so they carry
[value segments](../interfaces/pipes.md#value-segments): single-source `cat`
hands its file or HTTP reply to the next stage instead of copying its bytes.
Byte readers see the same stream through libc, `wc -c` counts such a value from
its size, and a failed source (such as a 404) fails the reader's input. Output
relay pipes stay byte-only, and the shell itself holds no values.

The first stage, when its selected stdin is a console, receives named terminal
input, keyboard and pointer grants as before. The final foreground stage also
receives named terminal input with READ alone when its stdin is a pipe or file
and its stdout is a console. It receives no interrupt-arming, keyboard or pointer
right through this exception. This lets `ls boot:// | less` read keys separately
from the pipe; readers can hold Ctrl+C passthrough with READ authority.

There is one console input queue. A pipeline whose first stage reads console
stdin while its final stage also reads console keys, such as `cat | less`, has
competing readers and is unsupported. Use a named file, redirected input or a
producer that does not read the console. Intermediate stages, a final stage with
non-console stdout, background jobs and services gain no input through this rule.
Separate terminal-output and display capabilities retain the
ordinary child policy. Programs explicitly using those capabilities can still
write to or draw on the terminal. In spaces that opt into
[ordinary child launch](init.md#boot-configuration), each foreground stage
receives LAUNCH through the shell's separate `child_launcher` grant, as `launcher`.
It receives no group supervision and no implicit pipe-creation authority. Only
when a shebang script names the shell interpreter `boot://init-shell.pxe` does the
script's shell also receive `child_launcher` and pipe creation (see
[script mode](#script-mode)). Shebang adaptation adds no other authority.

The shell parses and validates the entire pipeline, then opens all executable
images before opening redirect targets. It opens redirects in written order
across stages, creates the needed pipes and prepares grant storage, then truncates
outputs in written order. The batch launcher prepares every child before making
any runnable. A preparation failure starts none and identifies the failing stage
where available. Files created or truncated before failure remain changed, with
the same aliasing and independent-position limits described above.

Children receive only their selected endpoints. Unused ends close without being
delegated; after launch the shell closes every temporary pipe, image and redirect
handle before waiting. This lets a consumer reach EOF after its writers finish,
and a producer observe EPIPE after its readers finish.

The shell waits for every child and closes every observer before prompting.
Diagnostics identify stages, numbered from one, with nonzero exits or faults.
The **last stage**
determines pipeline success; an earlier failure does not override a successful
last stage. There is no pipefail option. In scripts, a failed last stage stops
the script, while an earlier failure followed by a successful last stage permits
the next line. Wait, cleanup and diagnostic I/O errors remain fatal to the shell.
An unknown launch outcome is also fatal because terminal input cannot safely
resume.

There is no job control. Ctrl+C terminates a running foreground command or
pipeline, as described below. Remote terminal END_INPUT supplies EOF after queued input;
local framebuffer input has no EOF operation. A child that waits on live terminal
input or ignores its pipe can keep the shell waiting even after its peers finish.
Disconnecting a remote session terminates its entire execution group.

## Interrupting foreground commands

A root shell, or a session successor, can stop its foreground job with Ctrl+C.
It receives the [interrupt right](terminal.md#interrupt-arming-and-passthrough)
on its `input` grant: from the kernel's initial console input through init,
`session` and the startup script locally, and from the terminal-create input
remotely. `session` launches pass the right on, because the successor replaces
the shell. Ordinary commands receive READ alone, so they cannot arm or observe the
terminal interrupt. They may separately opt in to a
[cooperative process notification](../interfaces/processes.md#cooperative-process-interruption). Scripts that hold the right arm their own foreground commands in
the same way.

For each foreground job, the shell:

1. Arms Ctrl+C before launching any stage, so a press during startup never
   reaches a stage as data.
2. Waits for every stage's completion and the interrupt together, through
   `wait_many` with 30-second deadlines from the shell's clock.
3. On the first Ctrl+C for direct foreground stages, acknowledges one pending
   terminal interrupt and requests cooperative interruption of each unfinished
   stage. A stage without a receiver is terminated immediately as before.
4. While any stage accepted the request, keeps waiting on completion and the
   same armed terminal handle. A second Ctrl+C forces the entire owned subtree
   to stop (or unfinished direct stages when there is no owned scope).
   No grace timer is introduced. Two presses in one injection remain
   distinct, and acknowledgment cannot erase a press arriving during dispatch.
5. After cooperatively interrupted direct stages finish, stops their remaining
   descendants and waits for subtree retirement. Collects actual results,
   restores presentation, then disarms before editing the next line.

Existing script, capture and output-helper scopes,
terminal hangup, explicit cancellation and cleanup keep forceful termination.
[TERMINATE](../interfaces/processes.md#implemented-process-completion) itself is
unchanged; only the separate cooperative request permits application cleanup.
Passthrough still makes Ctrl+C ordinary data while held.

Diagnostics and typed completion remain authoritative. A gracefully exiting
stage retains its real exit status; a forced stage reports `Process terminated`.
A Ctrl+C-cancelled foreground job ends the command list, including `;`, `&&`
and `||`, even after graceful exit. Pipeline completion follows its last stage.
A result already committed before
termination stays unchanged.

Interactive foreground commands and pipelines use an owned subgroup, including
descendants launched through their bound launcher. Normal exit or a fault in a
direct stage does not reset a terminal still held by its descendants: the shell
waits for them to finish. Cancellation stops that remaining subtree before a
prompt. Scripts retain their existing [subgroup](#script-mode); Remote disconnect
terminates its whole remote execution group.

Captures and machine/no-echo Remote sessions keep subtree ownership but skip
presentation normalization. After an ordinary interactive scope retires, the
shell normalizes only if a member or descendant held PASSTHROUGH at any point,
or the scope completed abnormally (fault, stop, escalation or hangup). Sticky
completed-group observations preserve raw use after its grant closes.
Normalization uses separately held terminal output: leave the alternate screen,
reset the scroll region and styles, show the cursor and start a fresh line. Redirected FILE/PIPE
streams receive no reset bytes. Input restoration comes from grant cleanup,
including on faults and forced stop; `atexit` is not required. Uncertain group
cleanup or failed normalization is reported and prevents a new prompt. This
restores terminal usability, not previous application contents or unsaved data.

Normal non-raw exits do not reset SGR colours, scroll margins or a hidden cursor,
as with ordinary Linux shells (accepted, chronium, 2026-10-23). Use these
zero-argument built-ins on the named terminal output:

- `reset`: end partial decoding, leave alternate screen, restore the full scroll
  region and primary cursor, reset SGR, show the cursor and start a fresh line.
- `clear`: clear the current screen and supported scrollback, then home the
  cursor. Mux retained history is erased; renderers without history ignore that
  part. Shell command-recall history is unaffected.

Both refuse missing/non-terminal output with a clear error and no control writes;
neither falls back to a redirected file or stdout. Existing restrictions on
built-in redirection, pipelines and background execution apply.

Input typed before Ctrl+C is discarded, so text typed into a hung command never
runs as the next shell command. Ctrl+C at the prompt still cancels the line,
because the shell is not armed while editing.

For a foreground graphical job, Super+Down shows the terminal and routes typing
to its normal 4 KiB queue while the game keeps running. The shell still waits for
the job and opens no second prompt; unread text waits for a later reader.
Super+Up restores graphics and clears unread terminal bytes. While the terminal
is shown, Ctrl+C reaches the shell's existing armed interrupt and terminates the
foreground job through the same authority and cleanup above. This adds no job
control or termination authority. See [graphics layers](../interfaces/graphics.md#choosing-the-visible-layer).

Programs that read lines through libterm hold passthrough only while editing a
line. In the Lua REPL, Ctrl+C cancels a typed line, but running code is
terminated. vi holds passthrough for its whole session, so Ctrl+C never ends
it or discards unsaved edits; press Escape and enter `:wq` to save and quit, or
`:q!` to discard changes.

- **Background, service and session launches** are never armed.
- **Without the right or a clock**, jobs wait without interruption, as before.
- **If arming fails**, the shell prints one diagnostic and runs the job without
  interruption.
- **Raw-keyboard programs** such as Doom receive key events rather than text
  while capture is the destination. Use their own controls, or show a foreground
  graphical job's terminal with Super+Down and use the armed Ctrl+C interrupt.

## Background commands

An unquoted, unescaped trailing `&` launches an external command without waiting:

```text
udp-echo 127.0.0.1 9000 --count 1 &
```

Whitespace before `&` is optional. Quoted `"&"` and escaped `\&` remain argument
bytes. A bare `&`, a conditional background list, or text after an unquoted `&`
is a syntax error; nothing on that line runs. Built-ins (`cd`, `exit`, `mount`, `session`) reject background
execution.

Background children receive no `input` or `keyboard` grant, so they cannot read
the shell's terminal or physical-key stream. Their standard input is unavailable,
not a fabricated EOF stream. Output, display and other ordinary child resources
are retained; output may interrupt the prompt, and there is no coordinated
redraw. The shell closes its process observer immediately. Existing process
cleanup reclaims the child's execution resources when it exits.

There is no job table, completion notification, exit-status collection, `jobs`,
`fg`/`bg`, signal delivery or termination command. Ctrl+C interrupts only the
foreground job, never a background command. Use programs with their own bounded exit policy.
Background launch also works in scripts: launch failure stops the script, while
successful launch lets it continue regardless of the child's eventual result.
Ungrouped local children may outlive the shell. The remote server terminates all
remaining session descendants when its root shell exits or the client disconnects.

Successful launch does not guarantee application readiness. For a UDP server,
wait for its listening message before sending traffic. Nothing starts in the
background during default boot.

## Script mode

A directly invoked script using the exact held Pyxis shell runs with its
launching shell's sources, so its commands have the same authority as commands
typed there. This applies to foreground, pipeline, background and `$(...)`
execution. A script cannot create missing sources or enable a space switch.
An explicit `shell.pxb` launch, including `shell home://post.pxs`, remains bounded
by its manifest and the caller's supplied grants; nested scripts retain that
resulting shell context. A shebang may name a `.pxb` bundle interpreter; it runs
as that bundle, with its manifest grants ([script launch](script-launch.md#bundle-interpreters)).

The script gets an owned subgroup; foreground commands and staging workers remain
inside its subtree, and Ctrl+C stops the subtree and waits for retirement. Each
child receives the launcher bound to its permitted scope. Ancestor supervision
and retention factories are not delegated.

A named READ resource `script` selects script mode. `argv[1]` supplies the name
used in diagnostics; the shell reads the granted handle from offset zero rather
than reopening that name. Further arguments are the script's arguments.
`shell home://post.pxs one two` passes them the same way. The script
reads one command per line with the same parser, `cd`, `exit` and foreground
execution as interactive mode, without prompts or command echo. Terminal input
stays available to foreground children.

Pyxis shell scripts use native typed variables and `service` commands rather
than sh syntax; by convention they use the `.pxs` extension,
not `.sh`; the packaged init scripts are `init/*.pxs` in the userland repository.
The shell does not check the extension: a script runs through its shebang.

Blank lines and whole-line `#` comments are ignored, including the shebang.
Spaces/tabs may precede the comment marker; embedded or quoted `#` is literal.
Each line allows 1024 bytes before LF, including CR in CRLF. Only CR immediately
before LF is stripped. A final line without LF is executed too. Oversized lines
and embedded NUL bytes are rejected without executing that line; comments have
the same bounds. A trailing backslash is an error, not line continuation.

A malformed command, unsuccessful conditional list, reported foreground fault
or read failure stops the script with failure. A `||` handler can recover ordinary
command, builtin or launch failure before that list ends.
For ordinary pipelines, the last stage determines success as described above;
HTTP body producers and response-reader pipelines check every stage. Shell
diagnostics include
`script-name:line:` (lines start at one); child diagnostics retain their own
format. EOF succeeds; `exit [N]` returns its selected status (default zero).
Neither falls back to an interactive prompt.

Script mode needs the same startup resources as interactive mode, including an
explicit launcher grant, except the console: only an interactive shell requires
console `input` and `output`. A script or `-c` line started with other standard
streams, such as Neovim's pipes, writes to those, and its commands inherit them.
The script retains the shell's available launch sources, including its separately
held `child_launcher`; all required startup resources must still be present.
Its ordinary children use the same native profiles and bundle declarations as
at the prompt. Plain background programs and services gain no launcher through
the ordinary-child profile; a declaring bundle can receive a scoped launcher.
A background script gains no console stdin or keyboard ownership. Existing
background-redirection restrictions still apply.

For ordinary child launch, the shell offers its `net_registry` (READ alone) only
to a bundle that declares it, such as [`net`](net.md); plain native programs
receive it through trusted session
handoff only. Ordinary TCP, UDP and echo copies retain their held interface
scopes. Resolver-capable children receive a separately scoped `dns_udp` grant
with their selected `DNS_SERVER` snapshot, subject to their declared grants.
Private policy, interface configuration and DHCP broadcast authority stay with
the trusted network-owner handoff, never ordinary native child launch. Recognizing
a shell script creates no network-owner role.

**Only trusted roots extend the trusted interpreter chain.** Trusted session,
boot and pane shells open the interpreter beneath their retained private
`interpreter_root`, independently of the mutable `boot` binding. A rootless
bundled shell (`shell -c` or `shell FILE.pxs`, including one started by Neovim)
retains a READ-only image from its own startup `app/bin/shell.pxe`; nested script
shells receive a private copy. This image selects code, not trust. Missing or
unusable image refuses script re-entry without falling back to a mutable name.
The held native FILE identity and retained interpreter selection identify the
shell, never a basename, suffix, manifest ID or rebound `boot` name. Other
interpreters keep their ordinary launch rules. A rootless shell cannot recover
an omitted trusted root, source or ownership role by re-entering its held image.

A handed script with `child_launcher` owns a foreground subgroup and keeps its
CONTROL. Its foreground commands use the bound launcher; completion, failure or
`exit` stops and waits for that subgroup. Background/services and successors use
the original permitted launcher outside it. Explicit `shell FILE`/`shell -c`
confines those launches under the positional script owner too. Interactive mode
has no script-wide group. An outer owner may still retire either kind of child;
there is no unconditional detached survival. See the shared
[script lifetime table](script-authority.md#lifetime), also used by Lua including
`os.exit`. Native group admission and final-CONTROL cleanup keep their existing
bounds. Remote waiting successors propagate their result before caller exit.

### Script arguments

A script's arguments are read only TEXT values from its argv. They are not
environment entries, so nothing publishes, exports or inherits them, and there is
no `shift` or `set`:

```text
#!boot://init-shell.pxe
cc=home://toolchain/clang/payload/clang.pxb
$cc "$@"
echo "$#" arguments, first "$1", tenth "${10}"
```

- `$1` to `$9`, and `${N}` for any N from 1, expand to one argument as TEXT and
  concatenate like other TEXT. `$10` is rejected with a hint to write `${10}`,
  and `$0` and `${0}` are rejected.
- A position beyond the arguments is an error that stops the script, like an
  unset variable. There is no default-value syntax.
- `$#` expands to the count in decimal.
- `"$@"` expands to every argument as a separate word, or to no word when there
  are none. It is the only expansion that produces more than one word, and it
  must be an entire double-quoted word: unquoted `$@`, `"x$@"`, an assignment from
  it and a redirection target are rejected, and so is `$*`. A command that expands
  to no words at all is an error.
- `$(...)` inside a script sees the script's arguments.
- Arguments stay TEXT. A CAP the caller passed arrives as its `name://` spelling,
  and the CAP itself is reached by name under the
  [typed value](#typed-values-and-command-sequences) rules; positional text never
  becomes a CAP again.
- An interactive shell has no arguments: `$#` is `0`, `"$@"` expands to nothing
  and `$1` is an error.

### Script runtime grants

The shell owns one launch-source inventory. Re-entering its exact held shell
retains that inventory; ordinary child preparation selects its native profile
or manifest declarations from it. There is no interpreter-specific extras list.
Held JIT, threads, PIPE CREATE, capture, clock, random, memory, display/audio and
other available sources keep their actual rights and transport masks. Held roots,
the command list and cwd keep their selected authority. These are existing
sources, not grants inferred from resource names or arbitrary handles.

Private job provisioning and retention authority remain private runtime state.
Process observers, ancestor group CONTROL, pending transactions, terminal
interrupt ownership and provider publication controllers are not launch sources
copied by shell re-entry. The script owns its script FILE and its own execution
and cleanup state. Standard streams use this invocation's redirects, pipeline
connections, append policy and observation companions. Exported TEXT/CAP entries
are an owned snapshot; private variables and interactive aliases are not exported.
Child assignments, exports and cwd changes do not mutate the parent shell.

A script is not a sandbox; use a restricted space when a sandbox is needed.
For example, a builtin such as `reboot`
works when its shell holds the required authority. An explicitly reduced native
grant list or bundle launch stays reduced; neither a script nor its descendants
can recover omitted sources. Ordinary native children keep their declaration/profile rules. The standalone
Lua interpreters use the same held-source inventory and scope routing; embeddings
and captured leaf jobs remain separate. The shell bundle declares optional JIT,
threads and random, so a declared JIT child works from a script whenever the same
child works from that script's launching shell.

The shell bundle declares optional private `job_provisioner` authority. When
available, exact held-shell execution receives a fresh private provisioning
subtree, including in pipeline, background and capture positions, under the
existing enclosing owner. It receives neither the caller's ancestor nor another
client's `job_work` leaf. This works recursively, including a rootless bundled
shell's held image. A manifest cannot create absent authority: with jobs off,
optional contexts stay absent and required `job_work` requests refuse before
launch. A client leaf cannot be converted into a provisioner. Runtime capabilities
`job_provisioner`, `interpreter_root` and `interpreter_image` cannot be used as
capability operands, copied into shell variables or exported. Startup retains the
runtime handles and removes their entries from the shell environment, so URI
spellings such as `job_provisioner://` cannot reach them. Capture destinations
naming a runtime role refuse before evaluating the command.

Each job client still receives a fresh leaf through its own declaration. A
script's process exit alone does not prove that a successor or service has
retired, so admitted delegation trees remain private until confirmed enclosing
owner cleanup or reboot. Captures and output relays retain delegation until
their owned group retires, then remove it. Plain foreground launches of the
startup-held shell bundle/image and exact held-shell shebangs also own and wait
a group, preserving the usual final-stage status. These exact-shell stages use
existing waiting-session behavior, so their session successors finish before the
original shell exits and the outer group seals; ordinary pipeline peers receive
no waiting-session marker. Native image matching uses
held FILE domain/object identity, not a name or manifest ID; the startup-selected
shell bundle is pinned for this containment comparison alone. Shebang selection
is parsed once and retained through admission. Each nested launch adds one group
level; the existing limits are depth 16 and 256 allocated group objects per
space, not a member quota. Arbitrary bundles declaring
provisioning outside an owned scope still retain their tree until pane/session
cleanup or reboot. A captured private-work directory does not extend storage
lifetime: group retirement clears/unlinks its contents. The script subgroup and
session/background/service routing above are unchanged; no outside-group executor is introduced. See
[bundle contexts](program-bundles.md#private-job-contexts) and the
[cleanup limit](https://git.internal/PyxisOS/pyxis-os/issues/1329).

When a bundle cannot start because a required grant is missing, or held with
fewer rights, the shell names it:
`shell: clang.pxb: Missing required resource threads`.

### Command lines

`shell -c "LINE" [argument...]` runs one command line as a one-line script:
the same conditional-list grammar, script subgroup and launch rules, and `$1`...
from the arguments after the line. An unsuccessful list stops execution before
the next `;`. Diagnostics are prefixed `-c:1:`. Explicit `exit [N]` selects the
native signed-32-bit status. Otherwise a failed foreground command's positive
exit status is preserved, other failure returns 1, and success returns 0. A line
containing CR or LF is refused, as is one beyond the script line limit.
Neovim's `'shell'` uses it for `:!`, `system()` and `:make`
([Neovim external jobs](neovim.md#external-jobs-and-shell-commands)).

Under `shell.pxb`, console `input` and `output`, `jit`, `threads`, `random` and the
`retention_account` are optional grants, so the command line runs with only
what its caller holds. Without a retention account, `$(...)` captures are
refused.

## Mounting roots

`mount [--optional] [--read-only | --read-write] host` uses init's scoped
`host_mount` resource to open the selected export and bind `host://` in this
shell. Access defaults to read-only; write grants authorize mutation attempts
but do not establish backend or host writability. It rejects an
existing binding. Optional mode skips only a missing resource; any actual mount
failure is an error, which stops a script. Ordinary shells receive the mounted
root rather than mount authority. See [host setup and lifetime](../devices/virtio-fs.md).

Native volume mounts use a separate init resource:

```text
mount [--optional] [--no-info] --partition N --volume NAME --read-only NAME://
mount --partition 1 --volume system --read-only data://
ls data://
cat data://share/hello.txt
data://bin/program.pxe
```

The `native_mount` authority selects the configured disk and bootstrap principal;
`--partition` is a positive one-based GPT entry number on that disk. The counted
volume name is 1–255 UTF-8 bytes and resolves once within the retained generation.
The binding name is chosen separately, so `data://` need not match the volume
name. The command requires `--read-only`; it acquires LOOKUP, ENUMERATE and
READ_FILES, plus FILESYSTEM_INFO when its mount authority holds OBSERVE.
`--no-info` explicitly withholds that observation grant. The lower-level mount ABI
can request a narrower root with LOOKUP,
rejects unknown rights and returns READ_ONLY for known mutation rights.

Destination validation, collision checks and binding storage reservation precede
acquisition. A conflicting root or service name fails without replacing it;
service collisions are checked again after acquisition, with the unpublished
root closed on failure. At most 16 roots are selected, including app/home/HOST;
overflow fails explicitly. Successful roots are added to the shell's explicit
selection and forwarded to children. Mount authorities are excluded from that
selection. No unmount, host refresh or hotplug administration is supplied.

`--optional` permits only a missing native authority, from disabled configuration
or confirmed hardware absence. Failures from present authority remain errors,
including wrong disk GUID, ambiguous/unusable hardware, invalid GPT/filesystem,
missing partition/volume and policy denial. See
[boot configuration](init.md#native-disk-configuration-and-mounting).
Observation grants permit the [scoped information query](../interfaces/directories.md#scoped-filesystem-information)
without granting content access. Read-only session forwarding preserves observation
when held; it cannot restore an omitted grant.

## Session handoff

`session program [arguments...]` launches a successor in the same space and on
its assigned CPU. Local callers exit successfully without waiting; remote
callers park until the successor completes, as described below.
For example, an init script can finish with `session boot://init-shell.pxe`. The
command also works interactively; failed launch returns to the prompt, while
script mode reports the script name/line and exits with failure as usual.
Program lookup and quoting use the ordinary command rules, including shebang
launch. The `session` word is removed from the child's arguments.

The successor receives copies of the usual terminal and memory grants, the
explicit selected root list and working-directory grants, current working-path
metadata and current environment, plus an explicit `launcher` resource preserving
the caller's LAUNCH authority (including scoped group creation). An optional `child_launcher`
is forwarded separately with LAUNCH alone. Other startup
resources, including the caller's `script`, `host_mount` and `native_mount`, are
not forwarded. Launching another script supplies that target's own READ script
grant through `program_launch`.

Successful launch ends script execution immediately: later lines do not run,
and the caller never reads terminal input again. A local caller closes its process
observer and exits, releasing only its own references; the successor keeps its own
references and can read input and launch programs after the caller is reclaimed.
There is no wait for application readiness: success means the launch was
accepted, not that the new program will initialize successfully. A later exit or
fault does not bring back the original shell or restart the session.

The remote server initializes the private `PYXIS_SESSION_WAIT=1` runtime policy.
Each shell captures it at startup and forwards it only to `session` successors,
including shebang scripts; ordinary commands, background jobs and services do
not inherit it. The marker creates no authority. A waiting caller keeps the
successor's observer, propagates its signed normal exit status and maps fault or
termination to failure. Script handoffs also propagate this outcome. No waiting
shell arms foreground interruption or reads input again, and `terminal_events`
is never forwarded. One shell remains parked per chained handoff, retaining its
stack, heap and grants until completion or disconnect cleanup.

This is explicit delegation with caller exit or a parked caller, without process
replacement or application readiness acknowledgment. The remote server still
terminates remaining descendants on root-shell exit and the entire group on
disconnect; the parked root lets a successor finish before that exit.

## Power-off and restart

`poweroff` and `reboot` are builtins that take no arguments. They need the
shell's `power` resource: local spaces opt in with `power = true`, and remote
root shells require the separate `remote_power = true`
([boot configuration](init.md#boot-configuration)). Live/PXE Remote enables it;
installed defaults do not. Without the grant they print
`this space has no power authority`.

The kernel stops running user programs without asking them to exit, writes every
mounted native pool's cached data and empties its journal, then powers off
through ACPI or restarts. Files written without `sync` survive; RAM volumes such
as `tmp://` do not. If a pool cannot be flushed, the command reports the status,
programs resume and the system stays up. Holding the physical power button
still switches off immediately, keeping only synced data. See
[ACPI power-off](../kernel/acpi.md#power-off-and-restart).

## Startup and child authority

The shell expects named `input`, `output`, `memory` and `launcher` resources,
plus `boot` and `tmp` roots. Input/output are separate console READ/WRITE grants;
memory permits MANAGE and launcher permits LAUNCH. Normally app supplies LOOKUP,
ENUMERATE and READ_FILES, and home additionally supplies CREATE, WRITE_FILES and
REMOVE. Directory grants can be restricted by the launcher; the shell queries
and preserves their actual rights.
An optional `power` resource supplies the `poweroff` and `reboot` builtins. The
shell retains it through exact-shell scripts and forwards it to a `session`
successor, not to ordinary native programs it runs;
session passes it on to local successors but not when it starts remote services.
The separate `remote_power` resource follows shell re-entry and session successors;
the remote supervisor converts it to `power` for its root shell. Ordinary
native commands and service providers receive neither through ordinary launch.
An optional `display` resource supplies DRAW authority for the space. An optional
`clock` resource supplies READ and SLEEP authority for monotonic time. An optional
`keyboard` resource supplies INPUT authority for physical-key sessions.
Additional selected roots, including optional HOST and native mounts, retain
the access granted by init. Root names do not determine permissions. The shell
preserves at most 16 startup roots and fails explicitly if that limit is exceeded.

An optional `child_launcher` resource controls delegation to ordinary foreground
commands, including every pipeline stage. The packaged Development and installed
`pyxis` profiles provide it; Read-only and Remote do not. When present, the shell
passes this grant as `launcher` with LAUNCH alone. Exact held-shell scripts
retain the available launch-source inventory and use launchers bound to their
own execution scopes. No inherited inventory carries an ancestor launcher.
Plain programs launched in the background
or as services gain no launcher through this policy. A bundle that declares
`launcher` is offered the grant in every launch mode, background included, as it
is at the prompt and in scripts alike. Trusted `session` handoff preserves
`child_launcher` as a distinct resource. If Remote explicitly opts in, that grant
is bound to the remote session's execution group.

The shell uses libc's retained [working-path context](process-state.md), seeded
from startup with its navigation boundary. Unknown descriptive spelling leaves
lookup usable and shows `[cwd unavailable]` in the prompt. The path is not resolved to replace the
chain: actual handles remain authoritative, and insufficient grants
fail normally. With no initial chain the shell starts at `tmp://`. Explicit
scheme changes use the bound root's actual grant; each descendant lookup retains
its parent's grant. Crossing a retained ancestor boundary fails as in the native
path API.

Each child receives independent standard-stream grants selected from the shell's
bindings, explicit redirects and pipe connections, with their declared
console/file/pipe protocols. Background children omit stdin.
Missing streams remain absent; stderr never falls back to stdout or the terminal.
The script interpreter and session handoff preserve these bindings too.

Each foreground child receives explicit copies of terminal output, memory and
the explicitly selected roots with their actual rights and transport masks, and
the current directory chain preserving each handle's rights independently.
Keyboard and pointer grants require the first stage's console stdin. Named
terminal input additionally follows the final-stage rule above. Optional ordinary
launch authority follows the separate `child_launcher` policy above. When
available, the [display](../interfaces/graphics.md),
[clock](../kernel/timekeeping.md), [random](../devices/randomness.md) and [keyboard](../devices/keyboard.md) grants are also forwarded
to eligible foreground children and session successors; background children omit keyboard input.
An owned snapshot of libc's current environment is forwarded explicitly, with
the existing network overlay. Assignments and `export` select what children
inherit ([typed values](#typed-values-and-command-sequences)); no automatic PWD
maintenance is added. Children receive current working-path metadata alongside
independent directory grants. Display normalization removes redundant separators
and dot components; lookup still walks the original input, so `missing/..` fails.

The shell never reads terminal input while waiting. Successful wait means child
resources have been reclaimed; it closes the observers, reports nonzero exits or
faults, and prompts again after every foreground child has completed. The terminal advances to a fresh line
only when its cursor is not already at column zero, preserving unterminated child
output without inserting an extra blank line after newline-terminated output.
Failed launch returns to the prompt; failed wait ends the shell because
input ownership can no longer be assumed. Fatal user faults are reported through
the existing completion kind, with details left in the kernel log. No descendant
supervision or terminal ownership mechanism is added.

## Startup settings

Trusted interactive startup applies per-home text settings and aliases from
`home://.config/shell/startup.lua`:

```lua
return {
  variables = { cc = "home://toolchain/clang/payload/clang.pxb" },
  exported = { EDITOR = "boot://vi.pxe" },
  aliases = { ll = { "ls", "-l" } },
}
```

`variables` entries act like `name=value` and stay private to the shell.
`exported` entries act like `export name=value`, so children inherit them. Both
tables and `aliases` are optional, and `return {}` changes nothing.

An alias value is a nonempty array of literal argument strings; its first string
is the command name or path and must not be empty. Alias names use ASCII letters,
digits, `.`, `_`, `+` and `-`; they are distinct from environment names. On an
unquoted, unescaped literal command word, the shell substitutes the alias's fixed
argument prefix once and appends the user's arguments. This applies to each
pipeline stage and to command substitutions in an interactive line. Configured
strings are never reparsed or expanded: `$name`, `*`, `;` and `|` stay argument
bytes, and an alias target is not expanded as another alias.

Aliases take precedence over external commands and builtins. Quote or escape the
command word to bypass them (`'ls'`, `\ls`), or use an explicit path. Tab offers
alias names, and `which NAME` prints an alias's literal argument prefix. Aliases
stay private to the interactive shell; child programs and script interpreters
receive no alias table. There is no executable startup hook or runtime alias
command.

The file is strict literal data. It is never run as Lua or shell code. Setting
values and alias arguments are strings of at most 4,096 bytes without NUL. Each
variable/exported name is a valid environment name and appears once across those
two tables. At most 16 variable/exported settings fit the existing environment
SET batch. Aliases are bounded by the same 64 KiB file and literal-parser limits;
expanded commands retain ordinary argument limits. Any other key, value type or
table shape refuses the whole file.

A name that currently holds a CAP, such as `tmp`, also refuses the whole file,
and so does a name the shell supplies itself:
- `home`, `path` and its `path.skipped` startup-diagnostic receipt;
- `DNS_SERVER` and `network.` names;
- the stdio `.append` names.

An existing TEXT value, such as an inherited `TERM`, is replaced. Every check
and alias allocation runs before one environment SET batch, so a refused file
applies neither settings nor aliases. The shell prints the reason and continues with its ordinary settings:

```text
shell: home://.config/shell/startup.lua: tmp holds a capability; none applied
```

The file is loaded once, before the first prompt, in shells that trusted startup
starts:
- a space's session shell;
- each new multiplexer pane;
- interactive Remote sessions, including each remote multiplexer pane.

The starting program opens the file through the home directory it already holds.
It passes the shell a private READ FILE named `shell_settings`. The shell takes
and closes that grant in every mode, so it never reaches children or a session
successor.

Scripts, `-c`, `--no-echo` machine sessions, services, init and rescue never
load the file. A `shell` started from a prompt receives no file. There is no
opt-out flag: a bad file applies nothing, so recovery is to edit or rename it, or
to start a manual `shell`.

Loading runs no command and acquires no authority. A writer of the file can
change the meaning of a command the user later enters, including builtin names
through aliases, or a value such as `$cc`. Alias targets still resolve with the
shell's current held command/path authority. The READ grant pins the file object, not
its bytes or its author.

Exported settings reach every child, and some children treat particular
variables as code:
- both Lua ports search `LUA_PATH` for `require`d modules;
- the Lua 5.1 interpreter also runs `LUA_INIT`;
- upstream Neovim runs `VIMINIT` and reads its configuration from
  `XDG_CONFIG_HOME`. The Neovim port forces `VIMRUNTIME`.

A writer of the file is therefore trusted with what such children run (owner,
2026-10-20: documented, not restricted). Exported text never replaces a capability that a launch supplies
itself: the shell and session skip inherited text whose name matches one of
their own entries, whether it came from this file or a manual `export`. As with
a manual export, a text name that matches no bound scheme reads as `NAME://`
text, and informational flags such as `PYXIS_REMOTE_MUX` can be overridden.

Each shell reads the file once, when it starts, so edits apply only to shells
started later. If the starting program finds the file but cannot open it, it
reports that on its own stderr. For a multiplexer or Remote that stderr is not
visible, so such a failure there is silent. Aliases are not supported.

## FAT32 mount management

An explicitly configured mount-only inventory enables:

```text
mount --fat32 --list
mount --fat32 --disk ID --partition N --read-write fat32://
sync --mount fat32://
release fat32://
```

Access is required (`--read-only` or `--read-write`). `ID` is a displayed
boot-scoped identifier, not a persistent selector. Mounting publishes an RO root;
RW data stays in the selected `fat32_write` source. Neovim's optional declaration
binds that source as `edit://`, so `nvim edit://FILE` edits without granting disk
or controller authority. Other bundle manifests may select another mount's
`NAME_write` explicitly. Undeclared plain programs inherit only RO views.

Configured controllers can be used with `sync --mount SOURCE` and `release
--control SOURCE NAME://`. Release validates the actual native domain, moves a
cwd on that volume to `home://` with a report, closes the caller's selected data
and original cwd grants, then retires. BUSY leaves other owners intact and retains
a controller; retry `release NAME://` after they drain. A missing/same-volume home
refuses the cwd move. Controllers also appear as `NAME_mount` selected sources.

The [thin utility and lifetime limits](../devices/fat32-mounts.md#trusted-workflow)
apply. Neither final control close nor shell exit synchronizes/retires implicitly;
normal clean power keeps its existing seal behavior with files open.

## Prepared HTTP requests

HTTP roots and configured endpoints use ordinary typed captures:

```text
api=$(http prepare POST json+http://example.test/api)
reply=$(printf '{}' > $api)
cat $reply
describe $reply
unset reply
```

`http prepare` performs no network I/O. Header-file credentials, root trust,
method and fresh per-use timeout follow the [provider contract](../interfaces/http-transactions.md).
A final nonappend stdout redirect to a PROVIDER or TRANSACTION uses the bounded
staging worker, including through renamed roots. Producers receive a pipe; the
worker receives the writable body FILE, while the shell retains transaction
control. Every producer and its owned subgroup retires before one COMMIT.
Failure aborts and binds a failed value, never a successful empty response or
another request. Capturing retains the completed FILE; `cat $reply` never issues
a fresh GET. Uncaptured output follows the same checks and discards a successful
reply; failure still fails the command.

The worker occupies one of eight batch stages. Append, stderr, background and
multiple transaction sinks are unsupported. Snapshot/body storage budgets and
native cleanup limits remain provider-owned; there is no automatic backend
fallback. General capability pipes remain C9.

### Retired request syntax

`=> name` and unquoted redirect-target `@name` are rejected during parsing,
before side effects, with the replacement spelling. Use `reply=$(producer > $api)`
and `cat $reply`; reading the retained reply performs no new GET. Quoted
`"@name"` is a literal filename. The unquoted rejection is
[transitional](https://git.internal/PyxisOS/pyxis-os/issues/1440).
`request` has no builtin meaning and uses ordinary command lookup.
