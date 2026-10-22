-- jq from the jq port.
return {
  id = "jq",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { jq = { path = "jq.pxe" } },
  entries = {
    { build = "ports/jq/stage/bin/jq.pxe", at = "jq.pxe" },
    { build = "ports/jq/stage/share/licenses/jq", at = "licenses/jq" },
  },
}
