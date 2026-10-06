# SHA-256 source

`sha256.c` comes from sbase `libutil/sha256.c`, pinned at
`c546c3a5724c81cee9a11d816a38ccdf17472129`:
<https://git.suckless.org/sbase/file/libutil/sha256.c.html>.
The file declares a public-domain implementation based on FIPS 180-3. Its
original notice and formatting are retained. `sha256_sum_n` is made static,
since only the ordinary 32-byte digest is exported. The accompanying
`../sha256.h` follows its public-domain context/interface with an include guard
and explicit `stdint.h` include.

The unmodified source file SHA-256 is
`575924340929a37051a045b20e17c9a7a3ff76ea4fcbd83b383a00684856d4aa`.

This is the host-only implementation. The Pyxis `xfer` program uses the
existing Mbed TLS PSA library instead.
