return function(inputs)
  local entries = {
    { tree = inputs.userspace, at = "" },
    { tree = inputs.ports, at = "" },
    { tree = inputs.sdk .. "/sysroot/usr/include", at = "sdk/usr/include" },
    { tree = inputs.sdk .. "/share/licenses", at = "sdk/share/licenses" },
    { tree = inputs.sdk .. "/share/toolchain", at = "sdk/share/toolchain" },
    { file = inputs.provenance, at = "sdk/manifest.txt" },
    { file = "third_party/limine/BOOTX64.EFI", at = "share/installer/BOOTX64.EFI" },
    { file = "boot/limine/limine.conf", at = "share/installer/limine.conf.template" },
    { file = "third_party/limine/LICENSE", at = "share/licenses/limine/LICENSE" },
  }
  for _, library in ipairs({ "crt0.o", "libc.a", "libterm.a", "libpyxis.a", "libgcc.a" }) do
    entries[#entries + 1] = {
      file = inputs.sdk .. "/sysroot/usr/lib/" .. library,
      at = "sdk/usr/lib/" .. library,
    }
  end
  if inputs.init ~= "" then
    entries[#entries + 1] = { file = inputs.init, at = "init", replace = true }
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
  return entries
end
