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
Implementation and native qualification remain pending.
