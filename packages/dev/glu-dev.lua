-- The Mesa OpenGL Utility Library over static OSMesa.
return {
  id = "glu-dev",
  variant = "9.0.3",
  kind = "dev",
  depends = { { id = "mesa-dev" }, { id = "libcxx-dev" } },
  entries = {
    { build = "ports/glu/stage/dev", at = "" },
  },
}
