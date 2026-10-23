-- Separate SDK ownership; retain the shared Lua-family notices/provenance.
return {
  id = "lua5.1-dev",
  variant = "5.1.5",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/lua51/stage/dev/include/lua5.1", at = "include/lua5.1" },
    { build = "ports/lua51/stage/dev/lib/liblua5.1.a", at = "lib/liblua5.1.a" },
    { build = "ports/lua51/stage/dev/lib/pkgconfig/lua5.1.pc", at = "lib/pkgconfig/lua5.1.pc" },
    { build = "ports/lua51/stage/dev/share", at = "share" },
  },
}
