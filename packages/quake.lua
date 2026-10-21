-- Shareware Quake for one system: both Quake ports and the shareware PAK.
-- Pack it with make game-packages, which supplies --build.
return {
  id = "quake",
  variant = "shareware-1.06",
  kind = "program",
  depends = {},
  commands = {
    quake = { path = "quake.pxb", command = "quake" },
    ["chocolate-quake"] = { path = "chocolate-quake.pxb", command = "chocolate-quake" },
  },
  entries = {
    { build = "ports/quake/stage/bin/quake.pxb", at = "quake.pxb" },
    { build = "ports/chocolate-quake/stage/bin/chocolate-quake.pxb", at = "chocolate-quake.pxb" },
    { build = "ports/quake/stage/share/licenses/quake", at = "licenses/quake" },
    { build = "ports/chocolate-quake/stage/share/licenses/chocolate-quake", at = "licenses/chocolate-quake" },
    { file = "../third_party/quake-shareware/pak0.pak", at = "id1/pak0.pak" },
    { file = "../third_party/quake-shareware/SLICNSE.TXT", at = "licenses/data/SLICNSE.TXT" },
    { file = "../third_party/quake-shareware/LICINFO.TXT", at = "licenses/data/LICINFO.TXT" },
    { file = "../third_party/quake-shareware/UPSTREAM.md", at = "licenses/data/UPSTREAM.md" },
  },
}
