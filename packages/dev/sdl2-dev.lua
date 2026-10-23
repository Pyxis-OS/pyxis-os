-- SDL2 headers and static archive, built by the SDL2 port for this system ABI.
return {
  id = "sdl2-dev",
  variant = "2.32.10",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/sdl2/stage/dev", at = "" },
    { file = "../../ports/LICENSE", at = "share/licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "share/licenses/pyxis-ports/LICENSING.md" },
  },
}
