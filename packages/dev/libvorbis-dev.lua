return {
  id = "libvorbis-dev",
  variant = "1.3.7",
  kind = "dev",
  depends = { { id = "libogg-dev" } },
  entries = { { build = "ports/libvorbis/stage/dev", at = "" } },
}
