# Terminal application follow-ups

Status: separate milestones chosen by the owner. Display drivers and
[space layers](../userland/space-layers.md) are implemented. The owner accepted
and authorized the first [terminal multiplexer](../userland/multiplexer.md)
slice on 2026-10-08: one window, up to eight equal/BSP panes and retained
scrollback. Its behavior and limits live in that reference. Later work below
remains unassigned and needs a proposal before implementation.

## Single-panel navigator

A navigator is an ordinary terminal application with its own location, history
and selection, usable alone or in a pane. It should browse directory capabilities
and schemes, select entries, launch an editor/viewer and redraw on terminal
resize. Navigator and file browser are one application. RAM, writable `host://`
and native npfs mounts provide concrete consumers.

PDCurses is a possible dependency to investigate through this application;
the multiplexer does not require it. Exact file operations and rendering/input
requirements remain open. Shells and editors remain independent applications.

## Operations between navigators

Independent navigators may expose endpoints for intentional interaction.
Proposed file operations exchange capabilities for selections and destination
directories; path strings do not grant authority. Sharing a layout grants no
automatic access to another pane. Endpoint shapes, scoped discovery, delegation
and operation ownership remain open.

Distinguish atomic rename from cross-backend copy-and-remove. Decide who performs
a copy, owns progress and handles cancellation/failure from concrete interactions.
These policies belong in a separate proposal, outside the multiplexer.

Additional multiplexer windows, ratio adjustment and other accepted first-slice
limits are recorded in [technical debt](../technical-debt.md#initial-terminal-multiplexer-limits).
