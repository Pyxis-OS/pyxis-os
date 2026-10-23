-- The selected static picohttpparser profile for the installed SDK view.
return {
  id = "picohttpparser-dev",
  variant = "f4d94b48",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/picohttpparser/stage/dev", at = "" },
  },
}
