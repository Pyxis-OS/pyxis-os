-- The Pyxis C SDK in sdk:// (docs/wip/sdk-view.md, decision 50): the boot
-- archive's SDK, which is libc, libpyxis, libterm, libfont, libsha256, libnpfs-format, crt0
-- and the compiler-rt builtins with their headers, plus pyxis.ld and pyxis.pc.
-- C++ is libcxx-dev's.
return {
  id = "pyxis-dev",
  variant = "1",
  kind = "dev",
  depends = {},
  entries = {
    { build = "sdk/sysroot/usr/include", at = "include", exclude = { "c++" } },
    { build = "sdk/sysroot/usr/lib/crt0.o", at = "lib/crt0.o" },
    { build = "sdk/sysroot/usr/lib/libc.a", at = "lib/libc.a" },
    { build = "sdk/sysroot/usr/lib/libclang_rt.builtins.a", at = "lib/libclang_rt.builtins.a" },
    { build = "sdk/sysroot/usr/lib/libfont.a", at = "lib/libfont.a" },
    { build = "sdk/sysroot/usr/lib/libsha256.a", at = "lib/libsha256.a" },
    { build = "sdk/sysroot/usr/lib/libnpfs-format.a", at = "lib/libnpfs-format.a" },
    { build = "sdk/sysroot/usr/lib/libpyxis.a", at = "lib/libpyxis.a" },
    { build = "sdk/sysroot/usr/lib/libterm.a", at = "lib/libterm.a" },
    { build = "sdk/sysroot/usr/lib/pyxis.ld", at = "lib/pyxis.ld" },
    { build = "sdk/sysroot/usr/lib/pkgconfig/pyxis.pc", at = "lib/pkgconfig/pyxis.pc" },
    { build = "sdk/sysroot/usr/lib/pkgconfig/libsha256.pc", at = "lib/pkgconfig/libsha256.pc" },
    { build = "sdk/share/licenses", at = "share/licenses" },
    { build = "sdk/share/toolchain", at = "share/toolchain" },
    { build = "sdk/manifest.txt", at = "share/manifest.txt" },
  },
}
