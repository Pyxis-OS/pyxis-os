# Lua port milestones

Status: deferred guest port, not a prerequisite for Kilo, TCC or host Lua recipes.
Revisit when useful, notably for system-wide configuration. The small milestone
boundaries below remain proposals; select runtime requirements first. They
consume the [SDK](../sdk-and-repositories.md) and [port recipe setup](ports-and-first-port.md).
Host Lua for recipes is independent of this guest port.

## 1. Pin Lua and define the runtime slice

Choose an upstream revision, preserve its license and review its actual build
requirements against Pyxis. Record the required C library, numeric, file and
terminal facilities, and the Lua libraries exposed in the first interpreter.

Completion: a pinned source recipe/metadata and a concrete list of missing
runtime facilities split into focused PR tasks. This is an audit milestone, not
an instruction to implement an entire libc or change kernel interfaces to POSIX.

Decide numeric requirements explicitly, including any needed floating-point
execution/context support. Do not quietly change Lua's numeric behavior to get
a build. Current stdio formatting lacks floating point; review conversion, math,
error handling and other requirements against the selected source.

## 2. Run Lua code in Pyxis

Implement the agreed minimal runtime requirements in focused changes, then build
the interpreter through the port recipe and SDK. Settle the exact execution
interface before starting, for example running a supplied expression with output
and an observable exit status. State which standard libraries are available.

Completion: ordinary builds and interactive boots demonstrate real Lua code
executing, printing a result and reporting an error. No system-wide configuration
integration, dynamic modules or complete standard-library promise is implied.

## 3. Scripts and an interactive REPL

Use native filesystem authority through libc/native adaptations to load scripts,
and use the terminal facilities for interactive input and output. Settle the
initial file/module lookup rules and supported libraries before implementation.

Completion: launch a script from the shell and use an interactive Lua REPL, with
useful file and language error reporting. Expand only the runtime facilities
needed for that slice. These may be separate PRs.

## Deferred

System-wide Lua configuration needs its own authority and library policy;
a configuration evaluator need not receive the same facilities as an interactive
program. Dynamic loading, broader library coverage, additional ports and Neovim
remain separate work. Do not couple the first Lua milestone to virtio-fs: the
existing initrd and RAM filesystem are sufficient places to supply scripts.
