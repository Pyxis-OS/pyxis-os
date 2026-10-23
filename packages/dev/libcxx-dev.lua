-- The C++ runtime in sdk:// (docs/wip/sdk-view.md, decision 51): libc++'s
-- c++/v1 headers, libc++, libc++abi, libunwind and libcxx.pc, built against
-- pyxis-dev's C library.
return {
  id = "libcxx-dev",
  variant = "1",
  kind = "dev",
  depends = {
    { id = "pyxis-dev" },
  },
  entries = {
    { build = "sdk/sysroot/usr/include/c++", at = "include/c++" },
    { build = "sdk/sysroot/usr/lib/libc++.a", at = "lib/libc++.a" },
    { build = "sdk/sysroot/usr/lib/libc++abi.a", at = "lib/libc++abi.a" },
    { build = "sdk/sysroot/usr/lib/libunwind.a", at = "lib/libunwind.a" },
    { build = "sdk/sysroot/usr/lib/pkgconfig/libcxx.pc", at = "lib/pkgconfig/libcxx.pc" },
    { build = "sdk/share/toolchain/LICENSE.TXT", at = "share/licenses/llvm/LICENSE.TXT" },
  },
}
