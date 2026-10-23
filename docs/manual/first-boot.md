# First boot and spaces

This page starts a freshly installed Pyxis and shows how to move around it.

## Start the system

1. Restart the machine without the install stick. The boot menu lists two
   entries and starts the first one after three seconds:

   ```text
   Pyxis OS (Caelum)
   Pyxis OS (rescue)
   ```

   Choose **Pyxis OS (rescue)** only to repair a system that does not start
   (Rescue).

2. Wait for the shell. Pyxis opens its own tab and shows a prompt:

   ```text
   home://>
   ```

   `home://` is your home directory. It is kept on the disk, so files you
   create there are still present after a reboot.

3. Type a command to check that the shell works:

   ```text
   home://> echo hello
   hello
   ```

## Spaces

A space is a separate environment with its own programs, files and screen,
shown as a tab along the top. An installed system has two:

| Tab | What it is |
| --- | --- |
| Caelum | The kernel's log. Read it when something goes wrong. |
| Pyxis | Your shell. |

Switch between them with the keyboard:

1. Press Super+Left to show the Caelum tab.
2. Press Super+Right to return to the Pyxis tab.

Super+Left and Super+Right stop at the first and last tab. The selected tab
has its name underlined.

At boot, Pyxis selects its own tab by itself. A key press or mouse click
during boot cancels that, and you stay on Caelum until you press Super+Right.

## Keep going

- [Shell basics](shell-basics.md) covers the commands you will use every day.
