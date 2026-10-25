-- LuaLS from the native lua-language-server port; opt-in installation.
return {
  id = "lua-language-server",
  variant = "1",
  kind = "program",
  depends = {},
  commands = {
    ["lua-language-server"] = { path = "lua-language-server.pxb", command = "lua-language-server" },
  },
  entries = {
    { build = "ports/lua-language-server/stage/bin/lua-language-server.pxb", at = "lua-language-server.pxb" },
  },
}
