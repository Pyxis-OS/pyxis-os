-- xfer from the Pyxis userland.
return {
  id = "xfer",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { xfer = { path = "xfer.pxe" } },
  entries = {
    { build = "userspace-root/xfer.pxe", at = "xfer.pxe" },
    { build = "ports/mbedtls/stage/share/licenses/mbedtls", at = "licenses/mbedtls" },
    { build = "ports/mbedtls/stage/share/licenses/tf-psa-crypto", at = "licenses/tf-psa-crypto" },
  },
}
