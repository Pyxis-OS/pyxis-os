return function(inputs)
  local entries = {
    { tree = inputs.userspace, at = "" },
    { tree = inputs.ports, at = "" },
    { file = inputs.firmware .. "/intel/ibt-20-1-3.sfi", at = "share/firmware/intel/ibt-20-1-3.sfi" },
    { file = inputs.firmware .. "/intel/ibt-20-1-3.ddc", at = "share/firmware/intel/ibt-20-1-3.ddc" },
    { file = inputs.firmware .. "/LICENCE.ibt_firmware", at = "share/licenses/intel-bluetooth/LICENCE.ibt_firmware" },
    { file = inputs.firmware .. "/WHENCE", at = "share/licenses/intel-bluetooth/WHENCE" },
    { file = inputs.firmware .. "/PROVENANCE.json", at = "share/licenses/intel-bluetooth/PROVENANCE.json" },
    { file = inputs.wifi_firmware .. "/intel/iwlwifi/iwlwifi-cc-a0-77.ucode", at = "firmware/ax200-wifi/iwlwifi-cc-a0-77.ucode" },
    { file = inputs.wifi_firmware .. "/LICENCE.iwlwifi_firmware", at = "share/licenses/intel-wifi/LICENCE.iwlwifi_firmware" },
    { file = inputs.wifi_firmware .. "/WHENCE", at = "share/licenses/intel-wifi/WHENCE" },
    { file = inputs.wifi_firmware .. "/PROVENANCE.json", at = "share/licenses/intel-wifi/PROVENANCE.json" },
    -- The guest SDK has no C++ headers; task 2c makes it the pyxis-dev package.
    { tree = inputs.sdk .. "/sysroot/usr/include", at = "sdk/usr/include", exclude = { "c++" } },
    { tree = inputs.sdk .. "/share/licenses", at = "sdk/share/licenses" },
    { tree = inputs.sdk .. "/share/toolchain", at = "sdk/share/toolchain" },
    { file = inputs.provenance, at = "sdk/manifest.txt" },
    { file = "build/kernel-random-NOTICE", at = "share/licenses/kernel-random/NOTICE" },
    { file = "build/kernel-wifi-NOTICE", at = "share/licenses/kernel-wifi/NOTICE" },
    { file = "build/kernel-amd-NOTICE", at = "share/licenses/kernel-amd/NOTICE" },
    { file = "assets/ui/volume/README.md", at = "share/licenses/volume-icons/NOTICE" },
    { file = "LICENSE", at = "share/licenses/volume-icons/LICENSE" },
    { file = "third_party/limine/BOOTX64.EFI", at = "share/installer/BOOTX64.EFI" },
    { file = "boot/rescue.list", at = "share/installer/rescue.list" },
    { file = "build/os-release", at = "os-release" },
    { file = "third_party/limine/LICENSE", at = "share/licenses/limine/LICENSE" },
  }
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
  for _, library in ipairs({ "crt0.o", "libc.a", "libterm.a", "libfont.a", "libpyxis.a", "libnpfs-format.a", "libclang_rt.builtins.a" }) do
    entries[#entries + 1] = {
      file = inputs.sdk .. "/sysroot/usr/lib/" .. library,
      at = "sdk/usr/lib/" .. library,
    }
  end
  if inputs.go_hello ~= "" then
    entries[#entries + 1] = { tree = inputs.go_hello, at = "gohello.pxb" }
  end
  if inputs.init ~= "" then
    entries[#entries + 1] = { file = inputs.init, at = "init", replace = true }
  end
  if inputs.network_config ~= "" then
    entries[#entries + 1] = { file = inputs.network_config, at = "config/network.lua", replace = true }
  end
  if inputs.live_config ~= "" then
    entries[#entries + 1] = { file = inputs.live_config, at = "config/live.lua", replace = true }
  end
  return entries
end
