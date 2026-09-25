# SipHash

Pinned reference implementation from https://github.com/veorq/SipHash,
commit `32d067603b93b47828700880649198e0bfbbcffa`.

The import contains `siphash.c`, `siphash.h` and `LICENSE_CC0`. We use the
upstream CC0 license option and retain the source notices and formatting.
No test programs, vectors, benchmarks or build system are imported.

Local adaptations: replace hosted assert with kernel KASSERT; replace the
header's inttypes/string includes with freestanding stddef/stdint; reject the
hosted DEBUG output mode. Algorithm and default 2 compression / 4 finalization
rounds are unchanged. Caelum uses the 64-bit output variant with 128-bit keys.

TCP identity policy lives in `kernel/net/lwip/identity.c`, not in this import.
