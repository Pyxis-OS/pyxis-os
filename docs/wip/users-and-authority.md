# Users and authority: design checkpoint

Status: working design discussion, updated 2026-09-29. Multiple users with
restricted permissions and the identity/authority separation below are agreed.
Persistent grants, prospective policy changes and the creation/move rules below
are also agreed; explicit revocation mechanisms remain open. This is not an
implementation assignment. See the [milestone index](boot-sdk-ports.md) and
[native filesystem design rules](../devices/filesystem-readonly.md#design-rules). The native v1 format has no
users or persistent permissions; it supersedes the former portable-core ownership
implementation. The identity and sharing directions below remain deferred until
a concrete consumer needs them.

## Agreed identity and authority direction

Identity claims establish authenticated facts; local policy interprets them;
capabilities carry runtime authority. Names, claimed identity and group membership
do not themselves authorize kernel operations.

- Support local authentication and eventual external identity providers through
  OIDC. Authentication resolves an admitted persistent Pyxis principal; creating
  a new principal is a separate provisioning decision, not an automatic effect
  of receiving a valid token.
- Map the trusted issuer and subject identifier to a stable Pyxis principal ID.
  Usernames, email addresses and display names are not ownership identifiers.
  Account recreation must not recover a deleted account's identity accidentally.
  Linking another authentication method/provider requires explicit authority.
- Persistent resource ownership refers to the Pyxis principal, independently of
  token expiry, account renaming or an authorized authentication-provider change.
  The retired portable core used opaque principal IDs; native v1 carries no
  principal or ownership fields. Future persistent identity/import rules need
  a concrete enforcement design before implementation.
- A user directory may eventually be local, remote or federated. It supplies
  admission, identity mappings and trusted attributes. External groups can map
  to local roles, but local machine policy determines their actual grants.
- PCA is the working name for the trusted machine/session authority broker.
  Its expansion is undecided. It holds explicit bootstrap/setup capabilities
  and constructs bounded sessions; its name or process identity confers no
  universal bypass. Broad capabilities still make it security-critical.
- Keep authentication, external claim interpretation and machine/session policy
  in userspace. The kernel enforces capabilities without interpreting JWTs,
  external groups or OIDC. Native mount authority remains explicitly delegated;
  a future broker needs its own concrete contract.
- Restricted applications cannot recover the user's full authority merely by
  identifying as that user. Access to PCA and particular grant requests must be
  controlled; sensitive elevation may require trusted user interaction. Login
  assertions and refresh credentials are not ordinary inherited application data.
- External login follows an appropriate authentication flow, including issuer,
  signature, audience, expiry and flow-specific binding/replay checks. An OAuth
  access token is not interchangeable with an OIDC ID token. Runtime file/object
  operations do not repeatedly present authentication tokens.
- Device authorization is a candidate for browserless login, subject to provider
  support for the required authentication flow. Local authentication need not
  manufacture JWTs. Preserve an explicit offline owner-recovery path.
- Login-token expiry, session lifetime and capability revocation are distinct.
  Directory/group changes do not implicitly revoke already-delegated grants.
  Logout, suspension and cross-session delegation need explicit policy.
- Becoming an identity provider, credential management and biometrics are later
  work. None is required to settle persistent ownership now. The parked
  [credentials and biometric unlock direction](credentials-and-biometrics.md)
  records the agreed TPM-sealed, root-factor-first trust model.

Protocol references: [OpenID Connect Core](https://openid.net/specs/openid-connect-core-1_0.html)
and [OAuth Device Authorization Grant](https://www.rfc-editor.org/rfc/rfc8628.html).

## Agreed persistent grants and prospective policy changes

- Persistent sharing policy names stable principals or locally managed groups,
  a resource/scope and object-specific rights. Begin with explicit allow grants,
  without deny precedence or global Unix-style permission classes. Default is
  no acquisition without a matching grant or an explicitly held administrative
  capability. Ownership/policy administration and storage accounting are separate.
- Policy governs acquisition through the trusted broker/storage authority.
  Ordinary operations and permitted delegation use the capabilities actually
  held. The broker can grant only authority available through its own explicit
  capabilities; a policy entry cannot manufacture authority.
- A directory grant must have a stated scope. The initial model is an
  explicit subtree grant: holding it authorizes permitted descendant operations,
  including future opens, without re-acquiring authority as the current user.
  Do not imply that changing a child policy filters such an existing grant.
  Creation and ordinary move rules are agreed below; nested access boundaries
  still need separate decisions before implementation.
- Editing sharing policy affects subsequent policy-based acquisitions. Existing
  grants and their permitted copies/delegations keep their authority. In
  particular, an existing directory grant can still open descendants within its
  scope. Removing a sharing entry must not be presented as immediate revocation.
- Account suspension blocks new admission and policy-based acquisitions once
  the change is authoritative locally. It does not by itself terminate processes
  or invalidate their capabilities. Remote policy caching/freshness is undecided.
- Explicit revocation is a separate operation/design. Do not promise it until
  its effect on derived grants, in-flight I/O, mappings and cross-session sharing
  is defined. Terminating a session releases its own resources but cannot recall
  copies delegated outside it or data already read.

The earlier portable format's ownership and policy-acquisition implementation is
retired. [Native mounts](../devices/native-readonly-filesystem.md) enforce held
capabilities without persistent identity or sharing records. Broker integration
and revocation remain separate future decisions.

## Agreed creation and namespace changes

- Start with parent-controlled creation defaults: a new object's policy owner
  defaults to the destination directory's policy owner. Creating data does not
  automatically grant the creator policy-administration rights. Attribution to
  a creator, if retained, is separate from ownership. Explicit ownership changes
  require administrative authority; allocation charges the containing volume.
- Placing an object beneath a directory exposes it to that directory's applicable
  subtree grants, including grants already held. Do not promise a private child
  inside a broadly shared subtree. Independent explicit grants remain distinct
  from access derived through namespace placement.
- Rename/move within a volume preserves object identity, ownership and independent
  explicit sharing entries. Require authority for source removal and destination
  insertion, plus replacement authority where applicable. Destination subtree
  access applies after publication. Moving out removes the old path, but does
  not revoke file or directory capabilities already derived through it.
- Copy creates a new identity under the destination's creation policy; it does
  not copy the source's owner or independent sharing entries by default. A
  cross-volume move is copy followed by removal, not identity-preserving rename.
- Namespace traversal must not escape a granted scope through parent traversal,
  aliases or mounts. Exact handling of those features, nested boundaries and
  transactional rename publication remains to be designed.

## Example consequences

- Two users can receive different private home roots. Sharing a machine or a
  username does not grant access to the other root. Namespace setup determines
  what each session calls `home://`.
- A restricted editor can receive one writable file without authority to acquire
  the user's entire home. Creating content does not imply policy administration.
- A shared project directory exposes new contents through its existing subtree
  grants. Removing Bob's sharing entry prevents a fresh policy-based acquisition,
  but Bob's existing directory capability can still open descendants. Moving a
  file out does not revoke capabilities previously acquired to that object.
- System maintenance requires explicit administrative capabilities. Neither a
  principal called administrator nor a process called PCA bypasses enforcement.

## Implementation checkpoint

Discuss the identity and authority model before committing persistent home
ownership, writable shared mounts, cross-user resource discovery or remotely
accessible services to a single-user assumption. Authentication and account UI
can be separate implementation milestones; ownership and grant rules need an
earlier checkpoint. The current init and host-mount slices do not need
a complete account system, but their prototype grants are not the eventual
multi-user policy.

The completed [native format and tools](../devices/filesystem-readonly.md) have no
user ownership or permission records. They do not authorize placeholder login
APIs, Unix IDs or mode bits, or implementation of an identity broker.

## Remaining decisions

- Native formatting supplies no bootstrap user ownership. Specify local
  authentication and bootstrap admission, plus the interfaces for
  establishing/changing a session's authenticated context; a recorded owner alone
  neither authenticates a principal nor bypasses acquisition authority.
- How do users relate to sessions, spaces and processes? A space is an execution
  domain, not automatically a user; one user may have several spaces. Decide
  which component creates them and supplies their initial resources.
- Define concrete broker contracts for initial grants, restricted launches and
  intentional delegation over the native capability interfaces.
  Identity must not widen authority; policy administration needs explicit rights.
- What does `home://` expose for each user? The earlier shared-home idea must not
  imply that all users can access one another's data. Decide sharing across one
  user's spaces separately from explicit sharing between users, including how
  ownership persists on disk. Today's [system layout](../userland/system-layout.md)
  has one shared `home` volume with no ownership, as a pre-users prototype;
  decide what becomes of its contents.
- Who may mount storage, publish system updates, administer accounts, inspect
  logs or manage other users' spaces? Decide administrative authority explicitly;
  no Unix root/group/ACL model is selected here. Two inputs from the system
  layout: write access to `system://config/boot.lua` chooses which inits run
  with forwarded grants on the next boot, so it is administrative; and raw
  volume access was agreed (2026-10-05) to be addressed by volume name and
  granted as a capability to rescue and administrative sessions only.
- Define logout, account removal and eventual explicit revocation. Preserve the
  agreed distinction between prospective policy changes and existing grants;
  termination cannot recall copies delegated outside a session.
- How does a virtio-fs mount relate guest authority to the host service's access?
  Host permissions do not by themselves define the Pyxis user model.

## Writable host prototype boundary

For the implemented [writable virtio-fs development mount](../devices/virtio-fs.md), trusted
boot-selected init scripts receive the available setup authority and delegate
read-write or read-only roots over the same export to their sessions. The kernel
enforces those grants and descendants cannot widen them. Mount authority stays
out of ordinary sessions; existing handles keep their rights after init exits.

The user explicitly selects the host export and daemon access. Guest operations
use the existing single host-service identity, constrained independently by host
permissions. This is a development mount, not persistent per-user home ownership
or a mapping from guest users to host accounts. Do not add placeholder identity
fields. The agreed future authority model does not change this prototype's
behavior; authentication, multiple guest identities and persistent ownership
enforcement still require implementation milestones.

## Completion of the design checkpoint

The model and example consequences above are agreed future design direction.
When a concrete multi-user consumer is assigned, identify its first implementation
slice, required metadata and broker/storage enforcement responsibilities. Native
v1 deliberately has no ownership enforcement; completing its writable-storage
milestone does not implement or require this deferred identity model.

## Related direction

- [Spaces](spaces.md) and [filesystem namespaces](vfs.md).
- [Initial session handoff](../userland/init.md).
- [First host mount](../devices/virtio-fs.md) and [later storage/services](later-os-directions.md).
