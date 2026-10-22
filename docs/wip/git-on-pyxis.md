# Git on Pyxis

Status: **direction decided (chronium, 2026-10-22); not implemented. Tasks are to
be planned after Go task 4.** No Git implementation runs on Pyxis, and no port,
source pin, ABI or compiler-container change is delivered by this document.
Builder/staging requests preempt this work. It supports [source builds](source-builds.md)
after hosted Clang.

## Decision (chronium, 2026-10-22)

Git on Pyxis is [go-git](https://github.com/go-git/go-git), a pure-Go
implementation of Git (Apache-2.0), used for both:

- **source fetching**, obtaining pinned source for recipes and source builds;
- **the Git CLI**, a developer's Git commands.

The libgit2 route and the C Git CLI proposals are dropped. The earlier
investigations are in Git history: libgit2 source acquisition (2026-10-09,
[#568](https://git.internal/PyxisOS/pyxis-os/pulls/568)) and the Git CLI and HTTP
transport (2026-10-11, [#711](https://git.internal/PyxisOS/pyxis-os/pulls/711)).
Obtaining pinned source stays separate from providing a developer's CLI as a
deliverable, even though both build on go-git. Neither automatically ports the
shell-based ports runner.

## Dependencies

go-git runs as Go on Pyxis, so it waits on the [Go runtime](go-runtime.md) tasks:

- **Go task 3, files and time:** native `os.File` and capability-root adapters,
  partial I/O, closure and errors, and timers. This is the filesystem go-git
  needs.
- **Go task 4, networking:** native sockets, readiness, cancellation and
  deadlines for TCP and DNS, feeding Go's netpoll. Its prerequisites are in the
  task: the readiness fan-in (many sockets and timers waiting together; the
  current `wait_many` takes a bounded number of interests, see [TCP](../devices/tcp.md))
  and the per-process thread limit (64 threads; see [hosted tools](../development/hosted-tools.md#clangd)).

Go tasks 1 and 2 are merged or qualified; the Go plan owns their status.

## Shape

- **Filesystem.** go-git reaches storage through its filesystem abstraction,
  [go-billy](https://github.com/go-git/go-billy). That is the place for a
  filesystem backed by a capability root, so a Git operation touches only the
  directories it was granted and no host path strings confer authority. Whether
  it sits on Go task 3's `os.File` adapters or directly on the capability calls is
  decided when the tasks are planned.
- **Transport.** HTTP(S) first. SSH comes later, after the
  [encrypted remote access](release-ideas.md#9-encrypted-remote-access-ssh)
  investigation settles Pyxis's SSH direction.
- **Trust.** A Git object ID verifies source identity relative to the trusted
  recipe pin; it does not authenticate recipe authors or replace TLS.

## Task status

Delivered libc prerequisites. They serve ports in general, not only Git:

- [x] Task 1: public `O_RDWR` and `O_CREAT|O_EXCL`; userland #171, Pyxis #573.
  See [qualification](../userland/libc-portability.md#readwrite-and-exclusive-create-qualification).
- [x] Task 2: public `pread` and `pwrite`; userland #172, Pyxis #576. See
  [positioned I/O](../userland/libc-portability.md#positioned-file-io-and-qualification).
- [x] Task 3: `lstat`, `access`, `rmdir` and the four `arpa/inet.h` byte-order
  functions; userland #174, Pyxis #585. See
  [path and byte-order checks](../userland/libc-portability.md#path-and-byte-order-qualification).
  The owner-accepted access profile 1 (2026-10-09) is recorded there.

Working path and environment (`chdir`/`getcwd`, `setenv`/`unsetenv`; children
receive explicit snapshots, no writable `environ` or `putenv`), the bounded
[`realpath`](../userland/paths.md#proved-realpath) and
[`fdopen`/`dup`](../userland/stdio.md#opening-and-ownership) are delivered by
[process state](../userland/process-state.md) and Neovim task 6 slice 2.

**Remaining tasks: to be planned after Go task 4.** Nothing further is assigned or
scheduled.

## Filesystem facts that carry over

Source-inspected from the earlier investigation; the authorities are the
[libc policy](../userland/libc-portability.md), [paths](../userland/paths.md) and the
[native writer](../devices/filesystem-native-adapter.md). They constrain any Git
storage on Pyxis, including go-git on Go's file layer.

- **Locks and publication.** Exclusive create gives a real `EEXIST`. A same-volume
  file rename replaces a file atomically. Data must be synced before a rename
  publishes a name; rename does not flush file contents.
- **Symlinks and modes.** The native filesystem has regular files and directories
  only; no symlink or chmod interface, no stored execute bit. Git tree modes and
  native launch authority are different concepts.
- **Directory sync.** Public `fsync` accepts only writable file descriptors; no
  directory bridge exposes native directory sync (see
  [atomic save limits](../technical-debt.md#atomic-save-limits)).
- **Enumeration.** Live, without dot entries or inode numbers, and may fail with
  `EAGAIN` after a mutation.
- **Names.** Native components are UTF-8, case-sensitive and at most 255 bytes;
  paths use `scheme://` roots rather than a leading `/`.
- **Earlier export defaults.** On 2026-10-09 (owner) a source export was accepted, for
  the libgit2 route, as of ordinary files and directories that rejects symlinks
  and gitlinks, into a fresh private `tmp://` store. Whether this carries over to
  go-git is decided when the tasks are planned.
