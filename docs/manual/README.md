# Pyxis user manual

Task-oriented pages for using Pyxis: each one gives the commands to type and
what you should see. Each release carries the manual that matches it. For why
something works the way it does, the pages link to the
[reference documentation](../README.md).

A title becomes a link when its page is written. The 0.0.8 core is the set of
pages that 0.0.8 ships with.

## Getting started

| Page | What it covers | Release |
| --- | --- | --- |
| [Install Pyxis](install.md) | Download and verify the release ISO, run it in QEMU and install onto a virtual disk; notes on real hardware. | 0.0.8 core |
| [First boot and spaces](first-boot.md) | Start the installed system, pick a space and find your way around it. | 0.0.8 core |

## Everyday use

| Page | What it covers | Release |
| --- | --- | --- |
| [Shell basics](shell-basics.md) | Run commands, use `*` globbing, add to a file with `>>`, complete names with Tab, recall history, set aliases, and clear or reset the terminal. | 0.0.8 core |
| [Files and URIs](files-and-uris.md) | Name files with `boot://`, `home://`, `system://config`, `pkg://`, `sdk://`, `bin://` and `tmp://`, and copy, move and list them. | 0.0.8 core |
| Editing with Neovim and vi | Open, change, save and quit in `nvim` and `vi`. | 0.0.8 core |
| Keyboard and the multiplexer | Switch spaces with Super+Left and Super+Right, split and move between panes with Ctrl+B, open the power screen with Ctrl+Alt+Delete, release a captured pointer with Super+Esc, and copy and paste with Ctrl+Shift+C and V. | 0.0.8 core |

## Configuration and networking

| Page | What it covers | Release |
| --- | --- | --- |
| Configuring the system | Edit `boot.lua`, `session.lua` and `network.lua` in `system://config` and check them with `config validate` before rebooting. | 0.0.8 core |
| Networking and Wi-Fi | Use wired DHCP, and join Wi-Fi on a running system with the `wifi` command (AX200 today); joins do not persist across reboots yet. | 0.0.8 core |

## Software

| Page | What it covers | Release |
| --- | --- | --- |
| [Packages with pkg](packages.md) | Install, list, remove and check packages, and choose repositories. | 0.0.8 core |
| [Updating with pkg upgrade](packages.md#updating-with-pkg-upgrade) | Stage a release and complete its package-set switch at verified reboot; use rescue or installer Update if the new boot fails. | 0.0.10 |

## Development

| Page | What it covers | Release |
| --- | --- | --- |
| [Compiling with Clang](compiling.md) | Build a C program with `clang` and `pkg-config`, using the `sdk://` defaults and `-dev` packages. | 0.0.8 core |

## Recovery

| Page | What it covers | Release |
| --- | --- | --- |
| Rescue | Boot Rescue, inspect an installed system and repair one that no longer starts. | 0.0.8 core |
