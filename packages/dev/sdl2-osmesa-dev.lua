return {
  id = "sdl2-osmesa-dev",
  variant = "2.32.10",
  kind = "dev",
  depends = { { id = "sdl2-dev" }, { id = "mesa-dev" }, { id = "libcxx-dev" } },
  entries = {
    { build = "ports/sdl2/stage/gl-dev", at = "" },
  },
}
