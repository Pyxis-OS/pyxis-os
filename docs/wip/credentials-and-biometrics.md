# Credentials and biometric unlock

Status: parked direction, 2026-10-02. Not an implementation milestone or
worklist. It follows the [users and authority checkpoint](users-and-authority.md)
and needs local users, USB and a TPM driver first. It authorizes no placeholder
APIs, structures or drivers.

The goal is biometric login on the first hardware target, the ThinkPad T14 Gen 1
(AMD), and a Pyxis-wide credential store protected by the same mechanism.

## Agreed trust model

- The credential store is encrypted with a random master key. Keys are never
  derived from biometric data: fingerprints are not secret, cannot be changed
  after compromise, and every scan differs.
- The TPM seals the master key under a measured-boot policy. Unsealing requires
  the configured authentication and an approved measured-boot state; authorized
  updates may introduce new approved measurements. The guarantee covers only
  what the boot chain actually measures.
- A password or PIN is the root factor. The first unlock after boot always uses
  it.
- Biometrics are a convenience gate after a root-factor unlock. A fingerprint
  match is a decision that lets a trusted service use the key; it never is the
  key.

## Proposed details

Everything in this section is open for review.

### Measurement and sealing

- Decide which component extends PCRs for the kernel, initrd and boot
  configuration. Firmware measures what it loads itself; a bootloader that reads
  the kernel as data must measure it explicitly.
- Prefer an authorized policy (`TPM2_PolicyAuthorize`) over sealing to raw PCR
  values, so a signed system update can authorize its new measurements without
  re-sealing every secret.
- Combine the measurement policy with the root factor (an authorization value),
  and rely on the TPM's dictionary-attack lockout for guessing limits.
- Keep an explicit offline recovery key. A cleared TPM, replaced board or
  unauthorized measurement change must not lose the store permanently. This is
  the owner-recovery path the users checkpoint already requires.

### Biometric gate

- Enrolling or removing a finger requires the root factor. Any enrollment change
  disables biometric unlock until the next root-factor unlock, so an attacker
  with brief access cannot add their own finger and keep using it.
- Require the root factor again after boot, after a configured idle period, and
  after a bounded number of failed matches. These are policy settings, not
  constants.
- The TPM cannot check a fingerprint itself. After the root-factor unlock, the
  credential service holds the unsealed key in memory, and biometric matches
  gate its use. The TPM protects against offline attacks and other operating
  systems; it does not protect against a compromised running Pyxis. State this
  limit wherever biometric unlock is described.

### Sensor trust

The following are investigation leads, not established properties of this
laptop. They depend on the actual sensor, its firmware and its configuration.

- The T14's reader may be a Synaptics match-on-chip USB sensor (vendor `06cb`);
  confirm the device from the hardware inventory. If it is match-on-chip,
  templates and matching stay on the sensor and the host receives only a match
  result for an enrolled template, so Pyxis would never handle fingerprint
  images.
- A sensor's answer is only as trustworthy as the channel. Without an
  authenticated host–sensor channel, a device impersonating the sensor over USB
  could report a match. Design for an authenticated, encrypted channel from the
  start. Determine whether this sensor supports Microsoft's Secure Device
  Connection Protocol (SDCP) and whether its firmware enables it. Published
  research on a Synaptics ThinkPad T14s reader found SDCP supported but shipped
  disabled, with a custom TLS channel used instead, which the researchers broke:
  [A Touch of Pwn](https://blackwinghq.com/blog/posts/a-touch-of-pwn-part-i/).
- Sensor template storage is shared with any other operating system on the
  machine. Recording which template IDs Pyxis enrolled does not establish
  ownership: another OS or a compromised enrollment interface can replace a
  fingerprint while keeping its ID. The same research did exactly that, enrolling
  an attacker's fingerprint under a legitimate user's template ID. Authenticated
  enrollment ownership is an unresolved requirement.
- libfprint may contain a reverse-engineered driver for the sensor. It is
  LGPL-licensed; decide deliberately whether to use it as reference only or to
  port it.

### Credential store as a capability service

- A userspace credential service owns the store and never releases the master
  key. PCA establishes the authenticated session; the credential service then
  issues capabilities scoped to particular items or collections.
- Where possible, applications use secrets through operations (sign, decrypt,
  derive) rather than receiving raw values. Exporting a raw secret is a separate,
  explicit right.
- Each application receives only the scope it was granted. Identifying as the
  user does not grant access to all of that user's secrets, which matches the
  existing rule that restricted applications cannot recover full user authority.
- Locking, logout and revocation are capability operations with the explicit
  semantics the users checkpoint still has to define.
- **SSH keys** are a first consumer (owner, 2026-10-08). The store holds private
  keys and serves signing as an operation, as an SSH agent does, for SSH client
  authentication and Git commit signing. A program receives a capability for one
  key or a set of keys, never the key itself, so a key can be granted to one
  space or agent session and withheld from others.
- Other consumers already point here: [Bluetooth bond keys](bluetooth-mouse.md)
  defer at-rest wrapping to this direction.

### Later

- Remote attestation of a Pyxis machine's measured state, for example to an
  identity provider or a homelab service. This is a natural extension of the
  same measurements but adds no requirement now.

## Dependencies

1. Local users, sessions and PCA from the [users and authority checkpoint](users-and-authority.md).
2. USB host controller (xHCI) support and device enumeration. The 40AJ dock also
   needs these.
3. A sensor protocol driver, including the authenticated channel.
4. A TPM 2.0 driver. The T14 has either a discrete TPM or AMD's firmware TPM;
   confirm from the inventory.
5. Measured boot: the PCR extension chain and policy above.
6. The credential service and its capability protocol.
