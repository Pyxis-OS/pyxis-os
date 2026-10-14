# CDC-NCM dock Ethernet

## Before-code baseline

Kernel source main `8395f988`, documentation head `e24c7472`; userland
`220b8076`, ports `efe8d877`, fs `d84f8084`, lwIP `a1aadb91`. Ordinary image
build passes with the existing `pyxis-llvm23.1.3-df87791` builder. No compiler
change. Performance configuration is `LOG_LEVEL=info`, QEMU 10.2.2/Q35/nested
KVM, CPU max, four CPUs, 2 GiB, UTC RTC, fresh OVMF variables, VirtIO network
and RNG. xHCI has a root USB 2 hub/keyboard and a Linux dummy_hcd/configfs NCM
gadget through usb-host. GDB inspects the unbound class function and a running,
nonfailed controller; no target calls or memory writes.

Remote client: `pyxis-remote --machine --no-shell-echo 127.0.0.1 24085`,
forwarded to guest 2323. Each IPC command is
`session bin://ipcbench.pxe MODE --size SIZE --messages 256 --rounds 100`.
All four warmups and 400 timed passes verify and exit 0 with complete drain.

| Nested time, ms | Median | Range |
| --- | ---: | ---: |
| CALL64 completion | 100.313 | 73.777–116.405 |
| CALL4096 completion | 100.430 | 74.582–106.510 |
| SEND64 completion | 14.059 | 12.389–22.549 |
| SEND64 admission | 1.210 | 1.134–1.625 |
| SEND4096 completion | 14.837 | 11.698–32.211 |
| SEND4096 admission | 1.251 | 1.135–2.674 |

A hands-free stream of 1,024 `echo -n` commands followed by `exit` reports
1,024 successful process completions plus successful exit; host-client wall
time is 15.03 s including connection and protocol overhead. This replaces the
old standalone `lua.pxe` loop: Lua is now a bundle and Remote has no job
profile. It is a wall proxy, not per-launch kernel time.

A prior trace-level capture also verified IPC, but idle logging substantially
widens its ranges; it is excluded from the performance comparison. Only the
info configuration above is the matched baseline. Raw captures, immutable
images and full commands remain in ignored `build/cdc-baseline{,-info}`.

Baseline ELF SHA256 `2ac7417e9e3ba23926a996f49e1d5f1af8b0285ced502e8eef596de866fe8123`;
ISO `859c63c2dda6846e6ba5d151f4571f290cb6a9dc0f80a6027e615fb71bd9167b`.
These are nested observations, not native performance or NCM driver evidence.
Matched after-code performance and native qualification remain pending.


## In-flight implementation checkpoint, 2026-10-14

Task 9 is incomplete. Kernel code `de624661`, parent pin `ca5fc713`, published
userland `e9edd5b4` ([#267](https://git.internal/PyxisOS/pyxis-userland/pulls/267));
other dependencies unchanged. Ordinary integrated image build passes with the
same builder. No compiler/container change.

Correctness uses the existing Linux 6.19.10 dummy_hcd/configfs NCM function
through QEMU usb-host: high-speed root function, real descriptor MAC
`02:77:00:00:00:02`, static guest `192.168.77.2/24`, peer `192.168.77.1`.
Q35/nested KVM, CPU max, 2 GiB, fresh OVMF variables and VirtIO devices match
the baseline. This is not an ASIX, SuperSpeed or dock-hub test. Linux's gadget
is an ordinary fixture, not new Pyxis test infrastructure.

The four-CPU trace build used the same kernel code before its commit and
userland `b526e4ac` (before the content-preserving rebase). Class negotiation
selected NTB16/no CRC, 16,384-byte RX/TX and real carrier notifications. GDB
observed prepared/ready/bound/up, a running nonfailed controller, zero malformed
NTBs and TX/two RX buffers within one 64 KiB span. Three pings received three
replies; a plain Remote command exited 0 with complete drain. Interactive xfer
published 5,848,400 bytes; remote sha256sum matched source ELF
`11f8cdf7fce6da4ca4639a358283743a3df2810c4111901db704567bda667d5b`.

The one-CPU info build uses `de624661` with rebased userland `e9edd5b4`.
Three pings received three replies; `echo ncm-one-cpu` through Remote exited 0
with complete drain. The existing host UDP log receiver observed the NCM MAC's
stream (3,959 bytes captured). Read-only GDB observed bound/ready/reported-up,
free copied queue slots, and running/nonfailed xHCI. No target calls or writes.

One-CPU ELF SHA256 `3f8b186e6cc6a11af6453c3af05863ed29604193c0036445decef9517984aa24`;
ISO `f00a3a795cb3a97fd55b384a6d516e4f2d11b9f8e271475047b7bab5dfe8bb20`.
Images and captures remain local in `build/ncm-first` and `build/ncm-checkpoint`.
These are correctness observations, not throughput or matched performance.

The ordinary gadget exposed two interoperability errors, both corrected:
control OUT completion reports its data-byte count; IN datagram alignment is
handled by byte-safe copies rather than rejecting the function's layout intent.
Whole-NTB bounds, overlap, termination and mandatory NDP alignment remain checked.

Remaining: bounded initial retries and known-branch reconstruction coordinator;
SuperSpeed/hub and hot-unplug/replug qualification; DHCP, malformed-input and
capacity review; matched after-code baseline; native ThinkPad/dock packet.
Recovery helpers/resources exist but no coordinator invokes them yet. Do not
stage this partial image as a normal native build. Both PRs remain draft.
