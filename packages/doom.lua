-- Shareware Doom for one system: both Doom ports and the shareware IWAD.
-- Pack it with make game-packages, which supplies --build.
return {
  id = "doom",
  variant = "shareware-1.9",
  kind = "program",
  depends = {},
  commands = {
    doom = { path = "doom.pxb", command = "doom" },
    ["chocolate-doom"] = { path = "chocolate-doom.pxb", command = "chocolate-doom" },
  },
  entries = {
    { build = "ports/doom/stage/bin/doom.pxb", at = "doom.pxb" },
    { build = "ports/chocolate-doom/stage/bin/chocolate-doom.pxb", at = "chocolate-doom.pxb" },
    { build = "ports/doom/stage/share/licenses/doom", at = "licenses/doom" },
    { build = "ports/chocolate-doom/stage/share/licenses/chocolate-doom", at = "licenses/chocolate-doom" },
    { file = "../third_party/doom-shareware/doom1.wad", at = "DOOM.WAD" },
    { file = "../third_party/doom-shareware/LICENSE", at = "licenses/data/LICENSE" },
    { file = "../third_party/doom-shareware/UPSTREAM.md", at = "licenses/data/UPSTREAM.md" },
  },
}
