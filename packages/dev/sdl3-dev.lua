-- SDL3 headers and static archive, built by the SDL3 port for this system ABI.
return {
  id = "sdl3-dev",
  variant = "3.2.28",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/sdl3/stage/dev", at = "" },
    { file = "../../ports/LICENSE", at = "share/licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "share/licenses/pyxis-ports/LICENSING.md" },
  },
}
