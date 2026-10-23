-- Separate SDK ownership; retain the shared Lua-family notices/provenance.
return {
  id = "luv-dev",
  variant = "1.52.1-0",
  kind = "dev",
  depends = { { id = "pyxis-dev" }, { id = "lua5.1-dev" }, { id = "libuv-dev" } },
  entries = {
    { build = "ports/lua51/stage/dev/include/luv", at = "include/luv" },
    { build = "ports/lua51/stage/dev/lib/libluv.a", at = "lib/libluv.a" },
    { build = "ports/lua51/stage/dev/lib/pkgconfig/luv.pc", at = "lib/pkgconfig/luv.pc" },
    { build = "ports/lua51/stage/dev/share", at = "share" },
  },
}
