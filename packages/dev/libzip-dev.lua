return {
  id = "libzip-dev",
  variant = "1.12.1",
  kind = "dev",
  depends = { { id = "zlib-dev" } },
  entries = { { build = "ports/libzip/stage/dev", at = "" } },
}
