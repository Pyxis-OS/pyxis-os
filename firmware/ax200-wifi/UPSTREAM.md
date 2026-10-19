# Intel AX200 Wi-Fi firmware

The unmodified CC-a0 API-77 image is pinned to linux-firmware 20260309,
commit `c822cbbb14ce5b8ee1f27346220640ac350bbf34`. `metadata.json` records
sizes, SHA-256 and the owner mirror. Fetch through that mirror only with
`scripts/fetch-ax200-firmware.py`; no upstream fallback. This pin is independent
of the AX200 Bluetooth firmware.

The archive carries the binary, `LICENCE.iwlwifi_firmware`, `WHENCE` and the
fetch provenance. The Intel licence permits unmodified binary redistribution
subject to its complete terms, including notices/disclaimer, no endorsement,
reverse-engineering restrictions and the OS patent condition. Firmware is not
covered by the kernel's MPL-2.0 licence.
