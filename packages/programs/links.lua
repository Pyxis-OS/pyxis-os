-- links from the links port.
return {
  id = "links",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { links = { path = "links.pxe" } },
  entries = {
    { build = "ports/links/stage/bin/links.pxe", at = "links.pxe" },
    { build = "ports/links/stage/share/licenses/links", at = "licenses/links" },
  },
}
