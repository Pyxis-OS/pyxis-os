-- The selected static utf8proc profile for the installed SDK view.
return {
  id = "utf8proc-dev",
  variant = "2.11.3",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/utf8proc/stage/dev", at = "" },
  },
}
