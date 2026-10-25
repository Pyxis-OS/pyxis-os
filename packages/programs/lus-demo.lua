return {
  id = "lus-demo",
  variant = "0.git.da6daf1a50a5",
  kind = "program",
  depends = {},
  commands = { ["lus-demo"] = { path = "lus-demo.pxb", command = "lus-demo" } },
  entries = {
    { build = "ports/libultraship/stage/bin/lus-demo.pxb", at = "lus-demo.pxb" },
    { tree = "../../ports/libultraship", at = "share/sources/pyxis-libultraship" },
    { tree = "../../ports/sdl2/pyxis", at = "share/sources/pyxis-sdl2" },
    { file = "../../ports/sdl2/SDL_config.h", at = "share/sources/pyxis-sdl2/SDL_config.h" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
