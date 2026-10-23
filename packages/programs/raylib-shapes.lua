return {
  id = "raylib-shapes",
  variant = "6.2",
  kind = "program",
  depends = {},
  commands = {
    ["raylib-shapes"] = { path = "raylib-shapes.pxb", command = "raylib-shapes" },
  },
  entries = {
    { build = "ports/raylib/stage/bin/raylib-shapes.pxb", at = "raylib-shapes.pxb" },
    { build = "ports/raylib/stage/dev/share/licenses/raylib", at = "licenses/raylib" },
    { build = "ports/sdl2/stage/dev/share/licenses/sdl2", at = "licenses/sdl2" },
    { build = "ports/sdl2/stage/dev/share/sdl2/source.txt", at = "share/sdl2/source.txt" },
    { build = "ports/raylib/stage/dev/share/raylib/source.txt", at = "share/raylib/source.txt" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
    { tree = "../../ports/sdl2/pyxis", at = "sources/sdl2/pyxis" },
    { file = "../../ports/sdl2/SDL_config.h", at = "sources/sdl2/SDL_config.h" },
    { file = "../../ports/raylib/pyxis_config.h", at = "sources/raylib/pyxis_config.h" },
    { file = "../ports/raylib/pyxis_raudio_sdl.h", at = "sources/raylib/pyxis_raudio_sdl.h" },
  },
}
