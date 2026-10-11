# Capability-valued environment variables

Status: **parked direction, 2026-10-11.** Not a milestone. It authorizes no code,
placeholder API or format change. Revisit it before any of these lands: native
HTTP prepared request objects (task 3) or their shell bindings (task 5), session
or space configuration changing the environment, [users and authority](users-and-authority.md)
work, or any other feature that stores a capability in a variable.

## The idea

An environment entry may hold a real capability, a transferred grant, instead of
only a string. The owner's words: storing capabilities in environment variables
"can span more than the shell. For example if a config document can set
environment variables, that means it should also be able to store capabilities in
them. Picture a user directory authorization that's maintained around in the
environment as a capability." In the original idea, a prepared HTTP request is a
capability that could be stored in a variable.

Entries would be inherited by children through launch, set by shells, and set by
configuration documents (session and space Lua). Examples:

- a prepared HTTP request, kept for a later submit or response read;
- a user directory authorization carried through a session;
- a credential-service item grant (see [credentials](credentials-and-biometrics.md)).

## What is true today

- Environment strings confer no authority, and the initial environment is a copied
  snapshot ([startup record](../interfaces/processes.md)). Libc copies it into
  mutable process-local storage ([process state](../userland/process-state.md));
  the values stay strings, and children get them by explicit forwarding.
- [Session configuration](../userland/session-configuration.md) already says its
  values are strings for now and that variables are expected to carry
  capabilities later.
- Prepared requests are proposed to reach the shell as a real grant through an
  authorized transfer, never a printed handle
  ([prepared requests and shell handoff](userspace-scheme-providers.md#prepared-requests-and-shell-handoff)).
  The native HTTP milestone (#720, still open) proposes private shell bindings as
  the first form, not environment entries.

## Invariants to keep

Phrased as constraints; where marked open, nothing is decided.

- A capability is never encoded as text, and a handle number is never printed or
  parsed back. Open: how a string-only consumer sees such an entry.
- Inheritance is explicit and can narrow rights, never widen them.
- Secret bytes (credentials) follow their own rule, separate from capability
  entries: a capability to use a secret is not the secret.
- A restricted program cannot recover authority from strings, whatever the
  environment contains.

## Open questions

For the owner later; none has an answer here.

- How does a configuration document name a capability it does not hold itself?
  Who resolves the name, and with what authority?
- What do libc `getenv` and ported programs see: absence, a placeholder or an
  error?
- Is a capability entry copied or moved on launch?
- How is one revoked, and what does a holder see afterward?
- How does this relate to launch grants and [namespaces](../interfaces/namespaces.md):
  an overlap to unify, or one mechanism?
