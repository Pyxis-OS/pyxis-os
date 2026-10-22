-- config from the Pyxis userland.
return {
  id = "config",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { config = { path = "config.pxb", command = "config" } },
  entries = {
    { build = "userspace-root/config.pxb", at = "config.pxb" },
    { build = "ports/lua/stage/share/licenses/lua", at = "licenses/lua" },
    { build = "ports/mbedtls/stage/share/licenses/mbedtls", at = "licenses/mbedtls" },
    { build = "ports/mbedtls/stage/share/licenses/tf-psa-crypto", at = "licenses/tf-psa-crypto" },
  },
}
