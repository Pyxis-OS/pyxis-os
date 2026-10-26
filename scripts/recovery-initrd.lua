-- boot/rescue.list owns the executable selection. The remaining entries supply
-- archive-default configuration and the selected programs' runtime data/licenses.
-- Personal image overrides, SDKs, game payloads and firmware never enter here.
-- The core boot archive (boot/initrd.lua) is this selection plus Install's files.
return function(inputs)
  local function exists(path)
    local file = io.open(path, "rb")
    if not file then return false end
    file:close()
    return true
  end

  local entries = {}
  for name in io.lines("boot/rescue.list") do
    local user = inputs.userspace .. "/" .. name
    local port = inputs.ports .. "/" .. name
    local has_user, has_port = exists(user), exists(port)
    assert(has_user ~= has_port, "rescue member missing or ambiguous: " .. name)
    local source = has_user and user or port
    if name:sub(-4) == ".pxb" then
      entries[#entries + 1] = { tree = source, at = name }
    else
      entries[#entries + 1] = { file = source, at = name }
    end
  end

  -- The rescue space reads only the archive's session, network and package
  -- settings; the init scripts and live spaces belong to DISTRIBUTION=full.
  for _, name in ipairs({ "session.lua", "network.lua" }) do
    entries[#entries + 1] = { file = inputs.userspace .. "/config/" .. name, at = "config/" .. name }
  end
  -- PACKAGE_REPOSITORY, written by scripts/package-config.lua.
  entries[#entries + 1] = { file = "build/packages.lua", at = "config/packages.lua" }
  -- The only certificate pkg trusts for signed repositories.
  entries[#entries + 1] = { file = "packages/signing/pyxis-signing-ca.pem",
    at = "share/pyxis/trust/software-signing-ca.pem" }
  entries[#entries + 1] = { file = "packages/signing/repository-floors.lua",
    at = "share/pyxis/trust/repository-floors.lua" }
  for _, name in ipairs({ "zoneinfo", "ca-certificates", "hwdata" }) do
    entries[#entries + 1] = { tree = inputs.ports .. "/share/" .. name, at = "share/" .. name }
  end
  for _, name in ipairs({ "busybox", "lua", "picohttpparser", "mbedtls", "tf-psa-crypto",
      "tzdata", "pciids", "usbids", "ca-certificates" }) do
    entries[#entries + 1] = { tree = inputs.ports .. "/share/licenses/" .. name,
      at = "share/licenses/" .. name }
  end
  for _, name in ipairs({ "random", "amd" }) do
    entries[#entries + 1] = { file = "build/kernel-" .. name .. "-NOTICE",
      at = "share/licenses/kernel-" .. name .. "/NOTICE" }
  end
  entries[#entries + 1] = { file = "assets/ui/volume/README.md",
    at = "share/licenses/volume-icons/NOTICE" }
  entries[#entries + 1] = { file = "LICENSE", at = "share/licenses/volume-icons/LICENSE" }
  entries[#entries + 1] = { file = "boot/rescue.list", at = "share/installer/rescue.list" }
  -- wifi-supplicant's RSN code.
  entries[#entries + 1] = { tree = inputs.userspace .. "/share/licenses/wpa-rsn",
    at = "share/licenses/wpa-rsn" }
  entries[#entries + 1] = { tree = inputs.userspace .. "/share/licenses/libsha256",
    at = "share/licenses/libsha256" }
  -- Rescue starts the HTTP provider with this script to fetch packages.
  entries[#entries + 1] = { file = inputs.userspace .. "/share/installer/system/init-services",
    at = "share/installer/system/init-services" }
  return entries
end
