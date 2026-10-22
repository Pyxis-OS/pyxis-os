-- http from the Pyxis userland.
return {
  id = "http",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { http = { path = "http.pxe" } },
  entries = {
    { build = "userspace-root/http.pxe", at = "http.pxe" },
  },
}
