-- The selected static lua profile for the installed SDK view.
return {
  id = "lua-dev",
  variant = "5.5.1",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/lua/stage/dev", at = "" },
  },
}
