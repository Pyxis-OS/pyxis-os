return {
  id = "sdl3-osmesa-dev",
  variant = "3.2.28",
  kind = "dev",
  depends = { { id = "sdl3-dev" }, { id = "mesa-dev" }, { id = "libcxx-dev" } },
  entries = {
    { build = "ports/sdl3/stage/gl-dev", at = "" },
  },
}
