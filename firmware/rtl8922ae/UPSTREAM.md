# Realtek RTL8922AE Wi-Fi firmware

The unmodified format-4 asset is pinned to linux-firmware 20260309, commit
`c822cbbb14ce5b8ee1f27346220640ac350bbf34`. `metadata.json` records the exact
size, SHA-256, upstream and mirror. `make rtl8922ae-firmware` fetches through the
mirror only, using the shared `scripts/fetch-firmware.py`; no upstream fallback.
`WIFI_RTL8922AE=1` adds the asset and complete licence, WHENCE and provenance to
the image. Kernel-only builds stay offline.

The asset is `rtw89/rtw8922a_fw-4.bin`, staged as
`firmware/rtl8922ae/rtw8922a_fw-4.bin`. Its format reference is Linux v6.18,
commit `7d0a66e4bb9081d75c82ec4957c50034cb0ea449`. The reference archive is
255,333,425 bytes, SHA-256
`2617efd103533b57613d16f144ceea575b1a6b890ce5e16201b1d254e2b7b33c`, from:

```text
https://repo.internal/repository/raw-github/torvalds/linux/archive/7d0a66e4bb9081d75c82ec4957c50034cb0ea449.tar.gz
```

The BSD source alternative and retained notices cover derived parser code;
firmware has its separate `LICENCE.rtlwifi_firmware.txt`, not BSD or MPL terms.
Preserve that licence verbatim with the binary, WHENCE and fetch provenance.
Stage 1 only inspects CPU-visible structure: it never executes firmware or
selects a runnable image/security key for a device. See the
[RTL8922AE plan](../../docs/wip/rtl8922ae-wifi.md).
