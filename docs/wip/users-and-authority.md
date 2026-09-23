# Users and authority: design checkpoint

Status: multiple users with restricted permissions are a confirmed requirement.
The model below is a discussion agenda, not a selected account format, permission
scheme or implementation assignment. See the [milestone index](boot-sdk-ports.md).

## When to design it

Discuss the identity and authority model before committing persistent home
ownership, writable shared mounts, cross-user resource discovery or remotely
accessible services to a single-user assumption. Authentication and account UI
can be separate implementation milestones; ownership and grant rules need an
earlier checkpoint. The current init and read-only host-mount slices do not need
a complete account system, but their prototype grants are not the eventual
multi-user policy.

Do not add placeholder user fields, Unix IDs, permission bits or login APIs in
anticipation. Turn the decisions into a focused milestone when that checkpoint
is reached.

## Questions to settle

- What represents a user, and who can establish or change a process/session's
  identity? How are local authentication and later remote sessions introduced?
- How do users relate to sessions, spaces and processes? A space is an execution
  domain, not automatically a user; one user may have several spaces. Decide
  which component creates them and supplies their initial resources.
- How do identity-based policy and existing object capabilities cooperate?
  Distinguish who a process represents from which operations its actual grants
  authorize. Define initial grants, restricted child launches, delegation and
  intentional sharing across users; a name or claimed identity cannot grant
  authority by itself.
- What does `home://` expose for each user? The earlier shared-home idea must not
  imply that all users can access one another's data. Decide sharing across one
  user's spaces separately from explicit sharing between users, including how
  ownership persists on disk.
- Who may mount storage, publish system updates, administer accounts, inspect
  logs or manage other users' spaces? Decide administrative authority explicitly;
  no Unix root/group/ACL model is selected here.
- What happens to processes, open handles and shared objects on logout, account
  removal or permission changes? Revocation, persistent ownership and accounting
  need explicit choices; do not assume changing a policy invalidates old grants.
- How does a virtio-fs mount relate guest authority to the host service's access?
  Host permissions do not by themselves define the Pyxis user model.

## Completion of the design checkpoint

Write down a concrete ownership and grant model with examples: two users with
separate homes, a restricted application, deliberate file sharing, and a
privileged setup operation. Identify the first implementation slice and any
required storage metadata before selecting persistent formats or expanding
sharing. Capability enforcement and account/authentication policy should have
clear responsibilities; their exact placement remains open.

## Related direction

- [Spaces](../spaces.md) and [filesystem namespaces](../vfs.md).
- [Initial session handoff](init-and-scripts.md).
- [First host mount](virtio-fs.md) and [later storage/services](later-os-directions.md).
