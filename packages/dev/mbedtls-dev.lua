-- The selected static mbedtls profile for the installed SDK view.
return {
  id = "mbedtls-dev",
  variant = "4.1.1",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/mbedtls/stage/dev", at = "" },
  },
}
