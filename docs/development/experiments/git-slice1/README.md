# Git slice 1 qualification

Functional QEMU qualification and the required matched-source horse/Linux
comparison have passed. Host and nested-VM measurements below have different
storage and network paths; they do not isolate OS overhead.

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

## Final-source clone comparison (2026-10-24)

Both builds use CLI `7eebc9153ba4409db35bb40fc8fa0fd2b1508c4c`, Go
`f93462692a63de8d56cc3059e94f558f253fbbbb`, the fixed module/patch graph,
`GOMAXPROCS=4`, amd64/v1, cgo off and `-buildvcs=false -trimpath`. Source hash
manifests, `go.mod`, `go.sum` and compiler revisions match. Linux additionally
compiles its OS-selected SSH registration dependencies. The SHA-256-checked
Linux entry is `381c7125962cd4d77a911e91e6272ae50df99807447bba80e73dbd70c685d31e`;
the native entry is the final P1F identified above.

All six fresh clones of `https://github.com/Pyxis-OS/pyxis-os.git` complete at
`0f4153cb4408c95a3c612d013f076e450f889134`. Ref observations before/after each
three-run batch agree. Horse runs precede the QEMU batch by a few minutes;
these batches are not interleaved. The final QEMU checkout also passes clean
status and log.

| Elapsed seconds | Horse / Linux host | Pyxis / nested QEMU |
| --- | ---: | ---: |
| 1 | 5.702 | 9.972 |
| 2 | 5.304 | 9.571 |
| 3 | 5.358 | 9.446 |
| Median | 5.358 | 9.571 |
| Range | 5.304–5.702 | 9.446–9.972 |
| Sample standard deviation | 0.216 | 0.275 |

Horse: CachyOS Linux `7.2.6-1-cachyos`, 24 logical CPUs, Btrfs destination.
Chronium's orchestrator runs the prepared helper, which verifies the binary
hash and records process creation through completed wait:

```sh
python3 ~/shared/present/git/horse-baseline/run.py
```

Pyxis: the four-vCPU nested KVM/invariant-TSC configuration and RAM-npfs home
above; outbound TCP uses QEMU user-mode NAT. The saved image's kernel/core is
`e57d9701e051`, with the unchanged final CLI delivered as a normal writable
`home://git-dev.pxb` development bundle. The guest checks the P1F SHA-256 before
measurement. Each command uses a fresh destination:

```sh
export GOMAXPROCS=4
home://git-dev.pxb clone https://github.com/Pyxis-OS/pyxis-os.git final-pyxis-1
```

Repeat with `final-pyxis-2` and `final-pyxis-3`. The existing Remote observer
measures command send through `command_complete`, including transport and
launch lookup. Horse measures process creation through wait. Neither batch
flushes caches, retries automatically or runs alongside a build. Cache state,
other host load and Internet conditions are uncontrolled. CPU virtualization,
RAM-npfs versus Btrfs, network paths and observation boundaries differ: the
median difference is not an isolated kernel/filesystem cost or a native-hardware
performance claim.

The earlier interleaved local Linux-VM/QEMU exploration used CLI `4627a9f174ba`
with the same subject HEAD: Linux 8.014/8.295/8.228 s, QEMU
10.480/9.861/9.800 s. It is retained only as earlier-source evidence, not folded
into the final-source statistics.

Raw commands, source/module records and logs stay under `/shared/present/git/`:
`horse-baseline/results-20261024T114004Z-pws3i73o/`, `final-clone-samples/`,
`clone-samples/` and `runtime/`. No native/PXE run, fault injection, automatic
stale-lock recovery or public package publication occurred. Persistent-device
performance and power-loss durability remain unqualified.
