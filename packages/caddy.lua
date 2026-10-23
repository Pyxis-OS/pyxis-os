-- Optional Caddy server with the standard module set and documented exclusions.
return {
  id = "caddy",
  variant = "2.11.7",
  kind = "program",
  depends = {},
  commands = { caddy = { path = "caddy.pxb", command = "caddy" } },
  entries = {
    { build = "ports/caddy/stage/bin/caddy.pxb", at = "caddy.pxb" },
    { build = "ports/caddy/stage/share/caddy", at = "share/caddy" },
  },
}
