-- OpenArena's unchanged baseoa release, with the offline ioquake3 bundle.
return {
  id = "openarena",
  variant = "0.8.8",
  kind = "program",
  depends = {},
  commands = {
    openarena = { path = "ioquake3.pxb", command = "openarena" },
  },
  entries = {
    { build = "ports/ioquake3/stage/bin/ioquake3.pxb", at = "ioquake3.pxb" },
    { build = "ports/openarena/stage/share/openarena/baseoa", at = "baseoa" },
    { build = "ports/openarena/stage/share/licenses/openarena", at = "licenses/data" },
    { build = "ports/openarena/stage/share/openarena/source.txt", at = "licenses/data/source.txt" },
  },
}
