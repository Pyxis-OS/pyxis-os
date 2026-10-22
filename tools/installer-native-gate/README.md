# Installer native-gate host checks

These are the read-only host helpers used for the owner-run installer gate.
They are not an installer, reservation, erasure tool or native test runner.
Run them only under the owner's scheduled disposable-media procedure.

`horse-check.py identity --state-dir ABSOLUTE-DIRECTORY` requires exactly one
unmounted, writable USB whole disk in the 64 GB class, without swap or holders.
It binds the exact byte count and SHA-256 of its whole-device by-id. Identical
reruns succeed; differing identities/sizes refuse without replacing the record.
Identical legacy records migrate from a raw by-id to its digest. Reports never
print the serial-bearing by-id. Keep actual identifiers out of PRs and notes.

Identity inspection then uses the unchanged native installer consent reader:
blank media passes; fully marked pools pass; unmarked or uncertain contents
veto this fresh-Install gate. Every live volume must have a regular root
`SAFE_TO_WIPE` file for the marked classification. Partial marking refuses
conservatively even though Read the room can admit it. An unmarked readable
nonempty pool is always a native installer veto. Refusal explains the explicit
owner-only blanking procedure, but no helper performs it or adds a marker.

`horse-check.py final --state-dir ABSOLUTE-DIRECTORY` rediscovers and checks that
same identity, then verifies the ESP through the supplied read-only FAT inspector.
It does not require markers on the newly installed pool. Its report checks
current/previous payloads, the boot record, menus and immutable rescue against the
packet's expected source/default-content hashes. Without a mid-run baseline it
does not prove arbitrary foreign-file preservation. Neither phase mounts or
writes media; only the explicit host state directory receives records/reports.

Build the host executables and copy helpers into an existing qualification packet:

```sh
tools/installer-native-gate/build.sh /absolute/packet-directory
```

The existing full image build must already have exported the port development
headers. This uses the host C compiler and the pinned fs format library, never
the target libc. `media-consent.c` supplies O_RDONLY/pread and progress suppression
to the unchanged userland consent reader; section garbage collection keeps only
its read path and the native CRC routine. The tools need Linux base util-linux /
coreutils, Python 3 stdlib, sudo and a compatible host libc; no FUSE or Python
packages. Preflight executes both inspectors' help before inspecting media.

The scheduled packet separately supplies `caelum.elf`, `initrd.cpio`,
`expected-install.json`, source provenance, checksums and an owner run sheet.
This build command neither assembles nor stages runtime images, and does not
create a target identity or infer expected installed hashes. Refresh the packet's
checksum manifest after updating helpers. No automatic native boot/write action.
