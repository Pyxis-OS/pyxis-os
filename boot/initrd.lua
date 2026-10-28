-- The boot archive. DISTRIBUTION=core is the core archive of
-- docs/wip/pkg-upgrade.md (decisions 18 and 64): the rescue set that
-- boot/rescue.list selects, as in the recovery archive, and what Install needs.
-- Every other program is a package. DISTRIBUTION=full keeps every program, the
-- init scripts and the live spaces, for QEMU and stick work (decision 20).
return function(inputs)
  local full = inputs.distribution == "full"
  assert(full or inputs.distribution == "core", "DISTRIBUTION must be core or full")
  assert(full or (inputs.init == "" and inputs.live_config == "" and inputs.go_hello == ""),
    "INIT, LIVE_CONFIG, IMAGE_HOSTNAME and GO_HELLO need DISTRIBUTION=full")
  local entries
  if full then
    entries = {
      { tree = inputs.userspace, at = "" },
      { tree = inputs.ports, at = "" },
    }
  else
    entries = dofile("scripts/recovery-initrd.lua")(inputs)
    -- The recovery selection already carries init-services.
    for _, name in ipairs({ "boot.lua", "session.lua", "network.lua", "init-installed" }) do
      entries[#entries + 1] = {
        file = inputs.userspace .. "/share/installer/system/" .. name,
        at = "share/installer/system/" .. name,
      }
    end
  end
  local common = {
    { file = inputs.firmware .. "/intel/ibt-20-1-3.sfi", at = "share/firmware/intel/ibt-20-1-3.sfi" },
    { file = inputs.firmware .. "/intel/ibt-20-1-3.ddc", at = "share/firmware/intel/ibt-20-1-3.ddc" },
    { file = inputs.firmware .. "/LICENCE.ibt_firmware", at = "share/licenses/intel-bluetooth/LICENCE.ibt_firmware" },
    { file = inputs.firmware .. "/WHENCE", at = "share/licenses/intel-bluetooth/WHENCE" },
    { file = inputs.firmware .. "/PROVENANCE.json", at = "share/licenses/intel-bluetooth/PROVENANCE.json" },
    { file = inputs.wifi_firmware .. "/intel/iwlwifi/iwlwifi-cc-a0-77.ucode", at = "firmware/ax200-wifi/iwlwifi-cc-a0-77.ucode" },
    { file = inputs.wifi_firmware .. "/LICENCE.iwlwifi_firmware", at = "share/licenses/intel-wifi/LICENCE.iwlwifi_firmware" },
    { file = inputs.wifi_firmware .. "/WHENCE", at = "share/licenses/intel-wifi/WHENCE" },
    { file = inputs.wifi_firmware .. "/PROVENANCE.json", at = "share/licenses/intel-wifi/PROVENANCE.json" },
    { file = "build/kernel-wifi-NOTICE", at = "share/licenses/kernel-wifi/NOTICE" },
    { file = "third_party/limine/BOOTX64.EFI", at = "share/installer/BOOTX64.EFI" },
    { file = "build/os-release", at = "os-release" },
    { file = "third_party/limine/LICENSE", at = "share/licenses/limine/LICENSE" },
  }
  if full then
    -- The recovery recipe brings these into the core archive.
    for _, entry in ipairs({
      { file = "build/kernel-random-NOTICE", at = "share/licenses/kernel-random/NOTICE" },
      { file = "build/kernel-amd-NOTICE", at = "share/licenses/kernel-amd/NOTICE" },
      { file = "build/kernel-intel-NOTICE", at = "share/licenses/kernel-intel/NOTICE" },
      { file = "assets/ui/volume/README.md", at = "share/licenses/volume-icons/NOTICE" },
      { file = "LICENSE", at = "share/licenses/volume-icons/LICENSE" },
      { file = "boot/rescue.list", at = "share/installer/rescue.list" },
      { file = "packages/signing/pyxis-signing-ca.pem", at = "share/pyxis/trust/software-signing-ca.pem" },
      { file = "packages/signing/repository-floors.lua",
        at = "share/pyxis/trust/repository-floors.lua" },
    }) do
      common[#common + 1] = entry
    end
  end
  for _, entry in ipairs(common) do
    entries[#entries + 1] = entry
  end
  if inputs.rtl_wifi_firmware ~= "" then
    entries[#entries + 1] = {
      file = inputs.rtl_wifi_firmware .. "/rtw89/rtw8922a_fw-4.bin",
      at = "firmware/rtl8922ae/rtw8922a_fw-4.bin",
    }
    for _, name in ipairs({ "LICENCE.rtlwifi_firmware.txt", "WHENCE", "PROVENANCE.json" }) do
      entries[#entries + 1] = {
        file = inputs.rtl_wifi_firmware .. "/" .. name,
        at = "share/licenses/realtek-wifi/" .. name,
      }
    end
  end
  if inputs.go_hello ~= "" then
    entries[#entries + 1] = { tree = inputs.go_hello, at = "gohello.pxb" }
  end
  if inputs.init ~= "" then
    entries[#entries + 1] = { file = inputs.init, at = "init", replace = true }
  end
  if full then
    entries[#entries + 1] = { file = "build/packages.lua", at = "config/packages.lua", replace = true }
  end
  if inputs.network_config ~= "" then
    entries[#entries + 1] = { file = inputs.network_config, at = "config/network.lua", replace = true }
  end
  if inputs.live_config ~= "" then
    entries[#entries + 1] = { file = inputs.live_config, at = "config/live.lua", replace = true }
  end
  return entries
end
