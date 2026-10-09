return function(inputs)
  local entries = {
    { tree = inputs.userspace, at = "" },
    { tree = inputs.ports, at = "" },
    { file = inputs.firmware .. "/intel/ibt-20-1-3.sfi", at = "share/firmware/intel/ibt-20-1-3.sfi" },
    { file = inputs.firmware .. "/intel/ibt-20-1-3.ddc", at = "share/firmware/intel/ibt-20-1-3.ddc" },
    { file = inputs.firmware .. "/LICENCE.ibt_firmware", at = "share/licenses/intel-bluetooth/LICENCE.ibt_firmware" },
    { file = inputs.firmware .. "/WHENCE", at = "share/licenses/intel-bluetooth/WHENCE" },
    { file = inputs.firmware .. "/PROVENANCE.json", at = "share/licenses/intel-bluetooth/PROVENANCE.json" },
    -- TCC compiles C only; the C++ headers stay out of the guest SDK.
    { tree = inputs.sdk .. "/sysroot/usr/include", at = "sdk/usr/include", exclude = { "c++" } },
    { tree = inputs.sdk .. "/share/licenses", at = "sdk/share/licenses" },
    { tree = inputs.sdk .. "/share/toolchain", at = "sdk/share/toolchain" },
    { file = inputs.provenance, at = "sdk/manifest.txt" },
    { file = "build/kernel-random-NOTICE", at = "share/licenses/kernel-random/NOTICE" },
    { file = "build/kernel-amd-NOTICE", at = "share/licenses/kernel-amd/NOTICE" },
    { file = "assets/ui/volume/README.md", at = "share/licenses/volume-icons/NOTICE" },
    { file = "LICENSE", at = "share/licenses/volume-icons/LICENSE" },
    { file = "third_party/limine/BOOTX64.EFI", at = "share/installer/BOOTX64.EFI" },
    { file = "boot/limine/limine.conf", at = "share/installer/limine.conf.template" },
    { file = "boot/rescue.list", at = "share/installer/rescue.list" },
    { file = "third_party/limine/LICENSE", at = "share/licenses/limine/LICENSE" },
  }
  for _, library in ipairs({ "crt0.o", "libc.a", "libterm.a", "libpyxis.a", "libnpfs-format.a", "libclang_rt.builtins.a" }) do
    entries[#entries + 1] = {
      file = inputs.sdk .. "/sysroot/usr/lib/" .. library,
      at = "sdk/usr/lib/" .. library,
    }
  end
  if inputs.init ~= "" then
    entries[#entries + 1] = { file = inputs.init, at = "init", replace = true }
  end
  if inputs.network_config ~= "" then
    entries[#entries + 1] = { file = inputs.network_config, at = "config/network.lua", replace = true }
  end

  local wad = inputs.wad
  if wad == "" then
    wad = "third_party/doom-shareware/doom1.wad"
    for _, name in ipairs({ "LICENSE", "UPSTREAM.md" }) do
      entries[#entries + 1] = { file = "third_party/doom-shareware/" .. name,
        at = "share/licenses/doom-shareware/" .. name }
    end
  end
  entries[#entries + 1] = { file = wad, at = "share/doom/DOOM.WAD" }
  if inputs.demos ~= "" then
    for _, name in ipairs({ "e1m1sec.lmp", "e1m2sec.lmp" }) do
      entries[#entries + 1] = { file = inputs.demos .. "/" .. name,
        at = "share/doom/" .. name }
    end
  end

  -- QUAKE_DATA paks arrive resolved, under the lowercase names Quake expects.
  local paks = { inputs.quake_pak0, inputs.quake_pak1 }
  if paks[1] == "" then
    paks = { "third_party/quake-shareware/pak0.pak" }
    for _, name in ipairs({ "SLICNSE.TXT", "LICINFO.TXT", "UPSTREAM.md" }) do
      entries[#entries + 1] = { file = "third_party/quake-shareware/" .. name,
        at = "share/licenses/quake-shareware/" .. name }
    end
  end
  for index, pak in ipairs(paks) do
    if pak ~= "" then
      entries[#entries + 1] = { file = pak, at = "share/quake/id1/pak" .. (index - 1) .. ".pak" }
    end
  end

  -- DIABLO_DATA opts in to DevilutionX with local shareware data. Its license
  -- allows personal use only, so ordinary and CI images never contain it.
  if inputs.diablo_spawn ~= "" then
    entries[#entries + 1] = { tree = inputs.devilutionx .. "/bin", at = "" }
    entries[#entries + 1] = { tree = inputs.devilutionx .. "/share", at = "share" }
    entries[#entries + 1] = { file = inputs.diablo_spawn, at = "share/diablo/spawn.mpq" }
  end
  return entries
end
