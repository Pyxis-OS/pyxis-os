-- mandelbrot, a development tool from the Pyxis userland.
return {
  id = "mandelbrot",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { mandelbrot = { path = "mandelbrot.pxe" } },
  entries = {
    { build = "userspace-root/mandelbrot.pxe", at = "mandelbrot.pxe" },
  },
}
