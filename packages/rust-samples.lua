-- Opt-in first-party qualification programs; never part of the core image.
return {
  id = "rust-samples",
  variant = "core-alloc",
  kind = "program",
  depends = {},
  commands = {
    ["rust-hello"] = { path = "rust-hello.pxb", command = "rust-hello" },
    ["rust-raster"] = { path = "rust-raster.pxb", command = "rust-raster" },
  },
  entries = {
    { build = "rust-samples/rust-hello.pxb", at = "rust-hello.pxb" },
    { build = "rust-samples/rust-raster.pxb", at = "rust-raster.pxb" },
  },
}
