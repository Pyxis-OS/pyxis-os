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
- The TPM seals the master key to a measurement of the booted system. A different
  boot chain or modified image cannot unseal it.
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

- The T14's reader is expected to be a Synaptics match-on-chip USB sensor
  (vendor `06cb`); confirm from the hardware inventory. Templates and matching
  stay on the sensor; the host receives only "enrolled finger N matched" or "no
  match". Pyxis never handles fingerprint images.
- A sensor's answer is only as trustworthy as the channel. Without an
  authenticated host–sensor channel, a device impersonating the sensor over USB
  could report a match. Design for an authenticated, encrypted channel from the
  start, such as Microsoft's Secure Device Connection Protocol (SDCP), which these
  sensors implement. Published research has bypassed readers that did not enforce
  it.
- Sensor template storage is shared with any other operating system on the
  machine. Pyxis records which template IDs it enrolled and for which principal,
  and ignores other enrollments.
- libfprint contains a reverse-engineered driver for these sensors. It is
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
