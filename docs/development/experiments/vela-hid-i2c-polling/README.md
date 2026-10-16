# Vela HID-over-I2C polling: task 3

## Pre-code baseline, 2026-10-16

Main `a6510a156efabc47e5986b5a603ec536f55cea2b`, existing df87791 builder,
matching CI2849 artifacts (Forgejo API run ID2949). Kernel SHA-256
`210be166047f8313a9a1eea1059a9e369891d5ba37d8b0e4830925c5ab979787`,
ISO `298e9ad6d08869ec674338eaf497410c833693dec80d9467322a5fec361067a8`.
Artifact metadata was checked against source/dependency revisions before use.
An earlier wrong API-ID download was rejected, not used as baseline evidence.

Nested KVM Q35/max, 4 CPUs, 2 GiB, OVMF, standard VGA, VirtIO-net/rng,
xHCI hub/keyboard; stock live spaces. Interactive read-only GDB: LPSS ABSENT,
claim NULL, master base zero. Remote uses the matching locally built client.
Four existing IPC commands (CALL/SEND, sizes64/4096, 256 messages, 5 rounds)
verify all samples and exit0/drain complete, followed by ten native echo
launches through Lua. No build overlapped timing; shared-host load uncontrolled.
Raw records: task worktree `build/task3-baseline/`.

| Nested baseline | Median ms (range) |
| --- | --- |
| CALL64 completion | 104.841 (98.992–122.802) |
| CALL4096 completion | 77.110 (73.720–96.666) |
| SEND64 completion | 13.188 (12.204–14.501) |
| SEND64 admission | 1.211 (1.199–1.236) |
| SEND4096 completion | 15.874 (12.290–23.842) |
| SEND4096 admission | 1.431 (1.261–2.078) |

These are nested controls, not native polling/CPU costs. Native no-polling
observations and the moving-finger interval sweep remain due. The owner chose
service-time min/median/max, wake/poll counts and `mean service × poll rate`
with scheduler/cache limitations; no scheduler probe is authorized for task3.
