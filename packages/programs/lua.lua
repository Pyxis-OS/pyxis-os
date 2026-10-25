-- lua from the lua port.
return {
  id = "lua",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { lua = { path = "lua.pxb", command = "lua" } },
  entries = {
    { build = "sdk/share/licenses/libsha256", at = "licenses/libsha256" },
    { build = "ports/lua/stage/bin/lua.pxb", at = "lua.pxb" },
    { build = "ports/lua/stage/share/licenses/lua", at = "licenses/lua" },
  },
}
