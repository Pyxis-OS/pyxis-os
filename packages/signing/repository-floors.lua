-- Verified against chronium's controlled signing trees and published copies.
-- Refresh with tools/freeze-repository-floors.py before a release.
return {
  version = 1,
  repositories = {
    { name = "public", url = "https://packages.pyxis.nexus/public/", signed = true, serial = 2, signed_at = "2026-10-25T20:30:16Z", sha256 = "c1f08889a2b1fc071ea26bd79ed4b22e481dbc12b0f2bc1bb72f25eeb78088a6" },
    { name = "releases", url = "https://packages.pyxis.nexus/releases/", signed = true, serial = 2, signed_at = "2026-10-25T20:30:38Z", sha256 = "c2185dc61d5772f42737d36826bb8d518f0656c176cb9871061d7f4f9325e243" },
  },
}
