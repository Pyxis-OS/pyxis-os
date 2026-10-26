-- Opt-in upstream demo and its complete native/static dependency notices.
return {
  id = "blurgtext",
  variant = "0.2.0",
  kind = "program",
  depends = { { id = "fonts" } },
  commands = {
    blurgtext = { path = "blurgtext.pxb", command = "blurgtext" },
  },
  entries = {
    { build = "ports/blurgtext/stage/bin/blurgtext.pxb", at = "blurgtext.pxb" },
  },
}
