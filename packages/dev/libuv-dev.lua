-- The selected static libuv profile for the installed SDK view.
return {
  id = "libuv-dev",
  variant = "1.52.1",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/libuv/stage/dev", at = "" },
  },
}
