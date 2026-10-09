# Filesystems and namespaces: working design draft

Status: working draft for discussion. This records a design direction, not an
approved specification or an implementation plan. Names, interfaces and policies
remain open. It complements the [spaces draft](spaces.md).

The [system layout](../userland/system-layout.md), agreed 2026-10-05 and
implemented 2026-10-07, supersedes the read-only shared base described below.
The system is a rescue archive plus writable system volumes. Program lookup
replaces an overlay for system binaries. The overlay, publication and bundle
ideas here remain open drafts, including write, deletion and copy semantics
wherever an overlay is used.

## Shared base and private overlays

All spaces would receive a shared, read-only system filesystem. Each space could
layer private overlays above it, with higher layers taking precedence during
name lookup. Installing an application in one space would change its overlay
without changing the base or other spaces.

For example, the base might supply `cat` and `ls`. A space needing `curl` could
install it privately and resolve it through the same application namespace.
Other spaces would continue seeing their own overlays over the shared base.

The eventual model allows stacked overlays. One private writable overlay over
the shared base would be enough for an initial prototype. Layer ordering and
the rules for sharing, attaching and removing layers remain to be defined.

Deleting a name inherited from a lower layer requires an overlay record that
hides that name. Simply removing the upper copy would expose the lower one
again. Directory merging, rename and replacement semantics remain open.

A concrete follow-up candidate is a
[host-backed development overlay](host-development-overlay.md)
above the boot archive, exposed through the existing `boot://` namespace. Host
builds could publish replacement programs without rebuilding the ISO, while
the guest keeps read-only access. This remains an opt-in experiment after the
plain virtio-fs mount; it does not implement the full private-overlay or system
publication model described here.

## Publishing system updates

An explicit operation could promote selected overlay content into the shared
system base. The initial discussion called this sync; publish or promote better
distinguishes it from saving private changes. It would be a privileged operation
because it changes the system view of every space.

The working model is a base that is read-only to consumers, updated by publishing
a new version. Publication should expose a complete update rather than a partly
installed application. Subsequent lookups in every space would use the new base;
existing open files and running programs could retain their old backing until
released. Publishing an update would not itself restart running programs.

Private overlays would still take precedence after publication. If the base has
curl A, one space overrides it with B, and C is published to the base, that space
would continue seeing B. A space without an override would see C. Publication
would not silently discard other spaces' private changes.

Publishing selected applications or changes is preferable to blindly merging
an entire overlay that may contain unrelated edits. The selection mechanism,
conflict handling, authorization, rollback and crash consistency are undecided.
So are the treatment of the publishing space's own overrides and reclamation
of old base versions. No on-disk format or versioning implementation is chosen.

## Shared writable storage

A separate read-write filesystem would be shared between spaces, serving the
role of personal or home storage. Changes there would be shared directly rather
than waiting for overlay publication.

This sharing is intentional: space isolation cannot protect a file from another
space that has write access to it. A later access model could expose only chosen
parts of shared storage to a space. Permissions, concurrent writes, accounting
and limits still need design.

## URI namespaces

Paths would use URI schemes to select logical namespaces rather than physical
filesystems. Candidate names include `app://` or `bin://` for applications and
`home://` for shared personal storage. These names and the precise URI syntax
are provisional; this draft does not assign authority or path components.

The calling space supplies the lookup context. An application name such as
`app://curl` could therefore resolve to different backing in different spaces,
using their overlays and the shared base. A scheme need not correspond to a
single storage device or filesystem implementation.

Knowing a URI would not grant access. Namespace resolution and authorization
must remain distinct, with the space's assigned resources determining access.
Native relative paths, escaping and normalization rules remain open.

The [userspace scheme-provider direction](userspace-scheme-providers.md)
extends this idea to endpoint-backed resources such as HTTP and HTTPS. The
kernel would own scoped bindings and route requests; protocol clients and TLS
remain entirely in userspace. Initial HTTP access would stage a complete body
under size/time limits before exposing a sized file, with response caching left
for later.

## Application bundles

The owner assigned a [first-slice bundle proposal](program-bundles.md) on
2026-10-09. It evaluates ZIP `.pxa` and unpacked `.pxb` forms exposing the same
per-program `app://`, manifest stack/resource/grant requests and larger executable
admission. It records the temporary grant policy and later consent/picker,
identity and revocation direction; implementation remains unassigned.

A future application could be a bundle exposed as a directory under `app://`,
containing its executable, private libraries, assets and defaults. The current
use of `app://` for executable lookup could move to `bin://`, separating command
names from installed application contents. These names remain provisional.

For example, `app://editor/program.pxe` and `app://editor/assets/` could belong
to one bundle, while `bin://editor` selects its declared entry point. A bundle
could expose several commands. How those command names map to entry points is
undecided; this does not require Unix symlinks or another executable copy.

“Bundle” describes the application unit, not its storage format. It might be an
archive, filesystem image or ordinary directory. The format, mounting/exposure
mechanism and possible manifest remain open. The useful contract is a directory
view with an identified entry point and application resources kept together.

The launcher could grant the application a directory capability to its own
bundle, so asset lookup would not depend on its installation name or working
directory. Writable user data would live outside the read-only bundle view.
Any manifest resource requests would remain subject to launcher authority;
package metadata could not grant itself capabilities.

Owner direction (2026-10-07): a bundle will carry a manifest, similar to a macOS
bundle's `Info.plist`, that declares the grants its program needs, for example
launch authority for compilers, `make` or `cmake`. Until bundles exist, launch
authority for ordinary commands is a per-space setting (`launch = true` in the
boot configuration), planned with the Lua runtime for build scripts.

This is a future direction, not a change to the first-shell milestone. Keep its
current initrd tree and `boot://` lookup while developing that milestone; no
bundle format, namespace migration or packaging implementation is assigned here.

### Signing, development spaces and requested grants

Parked ideas, not a settled security design or implementation task:

- Normal spaces would admit only signed application bundles. Official applications
  and ports could use a Pyxis trust root with authorized CI signing keys; key
  management and the trust mechanism remain undecided.
- Explicit development spaces would allow local/self signing and more permissive
  grant policy within their assigned authority. This would not relax other spaces
  or automatically confer machine-wide privileges. The current Kilo/TCC
  edit/build/run workflow would remain available there when signed admission arrives.
- Locally built applications could also run outside development spaces through
  explicit trust in their self-signed bundles. Time-limited approval/signing is an
  idea; thirty days was an example, not a chosen duration or expiry contract.
- A bundle manifest would declare requested capabilities, such as camera access,
  and distinguish essential from optional requests. Sensitive grants would need
  user approval, remembered for that user and application and revocable through
  system settings. The application could report a missing essential grant or
  continue with the affected optional feature disabled.
- Sensitive approvals and signing-trust changes would require a trusted user
  interaction that applications cannot forge or approve through synthetic input.
  The confirmation mechanism remains undecided.

A signature establishes provenance for admission; it does not grant capabilities.
The launcher/policy broker still supplies bounded runtime authority. Stable
application identity across updates is a shared open question for remembered
consent and future credential-service scopes under the
[users and authority direction](users-and-authority.md). Approval of newly
requested grants and revocation of capabilities already held or delegated need
later decisions; saved consent alone does not solve runtime revocation. How
signed interpreters or compilers execute unsigned scripts or generated code also
remains open. Bundle/manifest formats remain open; this note does not cover
measurements or boot trust.

## Future compatibility subsystems

Native Pyxis interfaces do not need POSIX semantics. In the distant future, a
compatibility subsystem could provide those semantics to applications running
in POSIX mode without imposing them on native applications.

`posix://` is a proposed filesystem view for that subsystem. Its syscall layer
could route absolute paths such as `/home` and `/etc` to `posix:///home` and
`posix:///etc`. Prefix translation would only select the view: the subsystem
would also define working directories, relative paths, permissions, links and
the other filesystem behavior its applications expect. The mapping from that
view onto native storage and namespaces remains undecided.

## Persistence and scope

Whether private overlays survive stopping or destroying a space is undecided,
as is their relationship to restarting a space and persistence across reboot.

A possible first implementation slice is resolving names through one private
overlay and a shared read-only base. Installation, publication, stacked-layer
management and compatibility subsystems can be separate later work.

This draft does not authorize implementing a VFS, adding placeholder interfaces
or restructuring the kernel. Implementation scope must be assigned separately.
