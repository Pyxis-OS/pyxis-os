return {
  id = "sdl2-mixer-dev",
  variant = "2.8.2",
  kind = "dev",
  depends = { { id = "sdl2-dev" } },
  entries = {
    { build = "ports/sdl2-mixer/stage/dev", at = "" },
    { file = "../../ports/LICENSE", at = "share/licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "share/licenses/pyxis-ports/LICENSING.md" },
  },
}
