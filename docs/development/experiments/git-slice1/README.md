# Git slice 1 qualification

Runtime and performance qualification is in progress. The required matched
horse/Linux baseline is not yet available: Tailscale SSH requests additional
authentication and LAN SSH refuses. No comparable performance result is claimed.

## Initial correctness run (2026-10-24)

Pyxis `b0b6f76e9028`, userland `8f2f572945da`, Go
`f93462692a63de8d56cc3059e94f558f253fbbbb`; go-git v5.19.3 and go-billy v5.9.2
with the recorded dependency patches. Later build-only corrections retain this
runtime code, except admission now permits literal colon/backslash characters
in a fresh child name, as native components do.

Nested KVM, q35, `max,+invtsc`, four vCPUs, 4 GiB, standard VGA, modern VirtIO
net/RNG and a read-only VirtIO-SCSI ISO. The live configuration changes `home`
to a RAM-npfs `git-work` volume within the existing 2 GiB sparse pool. No native
disk or virtio-fs. Git installs from a temporary unsigned local repository with
the normal `pkg` client and current command view; `GOMAXPROCS=4` is explicit.

| Check | Observed result |
| --- | --- |
| Public HTTPS Pyxis clone | `https://github.com/Pyxis-OS/pyxis-os.git`, HEAD `f265289aa4afc0d8d8f9bd33a0b789af95679762`; Linux and QEMU succeed. |
| History and fresh status | `git log -n 2` matches the recorded history; fresh tree is clean. Linux index retains original 100755 and 160000 modes. |
| Neovim same-size edit | Change `Pyxis OS` to `Pyxis os` on README line 1, save and quit. File stays 7,404 bytes; native status reports ` M README.md`. |
| Original Git symlink | `https://github.com/git-fixtures/symlinks.git`, HEAD `23c603542a49654726bc2c14c4b778f8c63f2862`. Both hosts clone and report clean; `bar` is regular target text `foo`, three bytes without newline. Linux index retains 120000. |
| Existing destination | Clone refuses without replacing the existing tree. |
| Unsupported HTTP URL | Refuses before creating a destination. |
| Unavailable public repository | HTTPS failure reports the error and retained partial directory; its initialized `.git` remains. |
| Debugger inspection | Read-only GDB check sees all four CPUs waiting in the scheduler; detach resumes normally. No injected calls. |

The first local Linux clone ran alongside compilation; its time is unsuitable
as a baseline. Source/compile checks establish propagation of ignore-file,
scanner, content-read and directory-list errors, but do not qualify live
enumeration mutation or power-loss durability.

Raw commands, Remote transcripts, module/source records, build logs and QEMU
configuration stay under `/shared/present/git/`. No native/PXE run, fault
injection, automatic stale-lock recovery or public package publication occurred.
The slice checkbox remains open pending the remaining authority/cancellation
checks and matched repeated clone measurements.
