-- lua5.1 from the lua51 port.
return {
  id = "lua5.1",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["lua5.1"] = { path = "lua5.1.pxb", command = "lua5.1" } },
  entries = {
    { build = "ports/lua51/stage/bin/lua5.1.pxb", at = "lua5.1.pxb" },
    { build = "ports/lua51/stage/share/licenses/lua51", at = "licenses/lua51" },
  },
}
