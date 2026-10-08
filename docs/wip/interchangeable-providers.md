# Interchangeable providers

Status: **owner direction, 2026-10-08.** Not scheduled and not assigned; nothing
here authorizes code. It came from thinking about the
[compositor](desktop-graphics.md#where-to-start-on-a-compositor), and applies
beyond it.

## Direction

A program depends on a **protocol**, not on who implements it. The program that
launches it decides which provider serves each resource. A compositor can then
give its clients a display, pointer and keyboard that behave exactly like the
kernel's, under the same names and through the same calls, while it holds the
kernel's real ones and multiplexes them. The client cannot tell, and does not
need to.

This is dependency injection in operating-system form. Programs declare what they
need by name; their launcher chooses the implementation.

## Precedents

- **Plan 9's rio** serves each window the same `/dev/draw`, mouse and keyboard
  files that the kernel serves rio itself. A program runs unchanged inside rio
  or on the bare screen, and rio runs inside another rio.
- **Genode** components ask their parent for sessions such as Framebuffer and
  Input. The parent routes each request to the real driver or to a GUI server
  that implements the same sessions.

## What Pyxis already has

- **Injected resources.** A program does not open "the display". It receives
  named startup resources (`display`, `keyboard`, `pointer`, `clock`, and the
  accepted `audio` grant) from its launcher, as
  [processes](../interfaces/processes.md#startup-record) describe. Today the
  launcher passes kernel objects.
- **Userspace providers of a kernel-shaped protocol.** The `text://`, `http://`
  and `https://` [file providers](../interfaces/file-providers.md) serve FILE
  capabilities from userspace. Shared libpyxis file helpers accept native and
  exported FILE handles alike, so `cat` and libc work with both.
- **Protocol identity.** Every message header carries a protocol tag, and
  [exported objects](../interfaces/userspace-services.md#interfaces-and-authority)
  carry kernel-authenticated object, protocol and rights metadata.

The direction extends what FILE already does to the display, input and other
device protocols.

## What it needs

1. **Memory shared between processes.** A display is memory the kernel maps for
   the program. A compositor must see each client's pixels without copying a
   frame per present, and Pyxis has no shared-memory capability yet. This is the
   first prerequisite, already noted in [desktop graphics](desktop-graphics.md).
2. **Provider-backed device handles.** `display_present`, `pointer_read`,
   `keyboard_read` and the other helpers must reach a provider when the handle is
   exported, as the file helpers do. `wait_many` must report readiness for those
   handles, so a client waits on a compositor's pointer exactly as on the
   kernel's.
3. **One protocol definition per interface.** Each protocol needs one documented
   contract that kernel and userspace providers both satisfy: operations, events,
   errors and lifetime. Extending it means adding operations in place, not
   coexisting versions, unless migration requires it. Clients and the
   [capability inspector](later-os-directions.md#developer-tools) can then check
   which protocol a handle speaks.
4. **Guarantees that stay in the kernel.** Super+Esc unlocking the pointer and
   switching spaces must keep working with a compositor in between. The kernel
   keeps the physical devices and those escapes; a compositor multiplexes only
   what it was granted.
5. **Who may act as a provider.** A compositor sees and can forge everything its
   clients see and type. Becoming one is a launch-time grant decision, in the
   same family as [Continuum](spaces.md#asterism) and the multiplexer's
   terminal-controller grant.

## Uses beyond the compositor

- Nested compositors, or a compositor running inside one space while another
  space uses the kernel display directly.
- Scripted input or a captured display for testing and recording a program, with
  no change to the program.
- Remote display and input, where the [remote desktop](remote-desktop.md)
  direction could serve a program's display and input over the network.
- Audio routed through a mixer service, if per-space kernel sessions prove too
  limited.

## Open questions

- Which protocols are first candidates. Display, pointer and keyboard serve the
  compositor; clock and audio may never need interposition.
- How a provider learns that its client has exited, and how a client sees that
  its provider has gone.
- Whether a client may hold both a provider's and the kernel's handle for the
  same protocol, or launchers always choose exactly one.
- Cost: every present and input event crosses an extra process. Measure the
  compositor round trip against direct kernel delivery before committing a
  protocol to interposition.
