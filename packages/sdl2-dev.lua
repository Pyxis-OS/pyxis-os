-- SDL2 headers and static archive, built by the SDL2 port for this system ABI.
return {
  id = "sdl2-dev",
  version = "2.32.10",
  kind = "toolchain",
  depends = {},
  entries = {
    { build = "ports/sdl2/stage/dev", at = "" },
  },
}
