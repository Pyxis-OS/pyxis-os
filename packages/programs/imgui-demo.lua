return {
  id = "imgui-demo",
  variant = "1.91.9b-docking",
  kind = "program",
  depends = {},
  commands = { ["imgui-demo"] = { path = "imgui-demo.pxb", command = "imgui-demo" } },
  entries = {
    { build = "ports/imgui/stage/bin/imgui-demo.pxb", at = "imgui-demo.pxb" },
    { tree = "../../ports/imgui", at = "share/sources/pyxis-imgui" },
    { tree = "../../ports/sdl2/pyxis", at = "share/sources/pyxis-sdl2" },
    { file = "../../ports/sdl2/SDL_config.h", at = "share/sources/pyxis-sdl2/SDL_config.h" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
