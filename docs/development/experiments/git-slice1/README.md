# Git slice 1 qualification

Functional QEMU qualification has passed. The required horse/Linux baseline
remains open: Tailscale SSH requests additional authentication and LAN SSH
refuses. Local Linux-VM/nested-QEMU samples below do not replace it.

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

## Final functional checks

The second boot uses Pyxis `e57d9701e051`, userland `4627a9f174ba` and the same
compiler/configuration, additionally carrying the tiny repository in the
read-only boot archive. A disposable copy of the manifest with TCP, DNS and
service grants omitted exercises the offline profile. Neither fixture is a
shipped package or a new authority interface.

| Check | Observed result |
| --- | --- |
| No network grants | Log/status succeed against the read-only archived repository. Clone into writable home fails on missing DNS authority and reports its retained partial child. |
| Read-only cwd | Clone refuses at directory creation; no partial destination is reported. |
| Retained cwd | Clone to literal `native:literal`, enter it, rename it to `native-renamed`; log and clean status still work through the retained context. |
| Ctrl+C | During HTTPS clone, returns `context canceled` and reports the partial destination. The shell remains usable. |
| Live enumeration | Concurrent ordinary Lua file creation in an ignored directory makes status fail with `readdir scan-directory: Pyxis: would block` (EAGAIN). A fresh status is clean after the writer finishes. |

After review, writable-file close was simplified to sync every write-access
handle, without per-method dirty tracking that an optimized `ReadFrom` could
bypass. Namespace creation and file write access are distinct. The final
userland `7eebc9153ba4` builds for both OS targets. Its P1F SHA-256 is
`83d2956a76c9171912509142a4ec3b187639b1dc91bd006db8062d7aa50ea5d3`;
an ordinary development bundle in writable home, with its entry and provenance
updated and the entry hash checked, passes tiny HTTPS clone/log/clean status.
The preceding close simplification `57dd367b84bd` also passes the full public
Pyxis clone/log/clean status at `0f4153cb4408c95a3c612d013f076e450f889134`.

## Exploratory nested samples

Interleaved Linux 1 / QEMU 1 / Linux 2 / QEMU 2 / Linux 3 / QEMU 3, all from
CLI `4627a9f174ba` and Go `f93462692a63`. Every fresh clone exits zero at
`0f4153cb4408c95a3c612d013f076e450f889134`; remote refs before/after agree.
Both set `GOMAXPROCS=4` and build with fixed vendoring, amd64/v1, cgo off,
`-buildvcs=false -trimpath`. Source manifests, module pins and compiler revision
match; Linux also compiles the OS-selected SSH registration dependencies.

| Elapsed seconds | Linux VM | Nested QEMU |
| --- | ---: | ---: |
| 1 | 8.014 | 10.480 |
| 2 | 8.295 | 9.861 |
| 3 | 8.228 | 9.800 |
| Median | 8.228 | 9.861 |
| Range | 8.014–8.295 | 9.800–10.480 |

Linux runs in the 12-logical-CPU VM on Btrfs. QEMU has the RAM-npfs backing and
NAT configuration above. Linux measures process creation through wait; QEMU
measures Remote command send through `command_complete`, including transport.
Destinations are fresh, caches are warm/uncontrolled, builds were held idle and
other host/network load was uncontrolled. Different backing and network paths
prevent an OS-only performance conclusion. These samples precede the final
close cleanup and are not final-head or horse measurements. The first Linux
smoke clone ran alongside compilation and is excluded.

Raw commands, Remote transcripts, module/source records, build logs and QEMU
configuration stay under `/shared/present/git/`, including `clone-samples/`.
The portable `horse-baseline/README.md` gives the single host command; it has not
been run. Record that result and matching final-source QEMU samples before
closing the slice. No native/PXE run, fault
injection, automatic stale-lock recovery or public package publication occurred.
Persistent-device performance and power-loss durability are unqualified. The
slice checkbox remains open for the required matched horse comparison.
