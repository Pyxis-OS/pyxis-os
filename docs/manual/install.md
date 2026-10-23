# Install Pyxis

This page installs Pyxis on a whole disk. It erases everything on that disk,
including any other operating system. The installer downloads the programs, so
the machine needs wired or dock Ethernet. Wi-Fi is not available during
installation.

## Before you start

1. Build the install image from a Pyxis checkout:

   ```sh
   make -j16 usb-image BOOT_MENU_TIMEOUT=5
   ```

   The image is `build/pyxis-usb.img`. `BOOT_MENU_TIMEOUT=5` keeps the boot
   menu on screen for five seconds, which is long enough to choose Install.

2. Write the image to a USB stick. Check the device name first: the command
   overwrites it.

   ```sh
   lsblk
   sudo dd if=build/pyxis-usb.img of=/dev/sdX bs=4M conv=fsync status=progress
   ```

3. Plug in the Ethernet cable, then boot the machine from the stick.

## Start the installer

1. The boot menu lists two entries. Press Down to select **Install Pyxis**,
   then Enter.

   ```text
   Pyxis OS (rescue)
   Install Pyxis
   ```

2. The screen shows the kernel log in the **Caelum** tab. If the **Install**
   tab does not open by itself, press Super+Right.

   The installer starts the network and shows its first list:

   ```text
   Starting the network for package downloads...
   1. Install
   2. Update
   3. Replay
   4. Cancel
   Choose installer action
   ```

   Select a line with Up and Down, or type its number, then press Enter.
   Enter alone takes the highlighted line.

## Install

1. Choose **1. Install**.

2. Choose the layout. **Classic** uses the whole disk. **Coexist** keeps other
   partitions and is described in [native installation](../userland/installer.md).
   Type `2` and press Enter.

   ```text
   1. Coexist
   2. Classic
   3. Cancel
   Choose installation layout
   ```

3. Choose which disks the installer may offer. **Proceed with marked media**
   offers only disks marked as safe to erase. **Read the room** also offers
   blank disks and disks with other contents, which is what you want for a
   new disk. Type `2` and press Enter.

4. The installer checks the package repository and lists the eligible disks.
   If there is one, it selects it. Check the size before you go on:

   ```text
   pkg: public/base@1+f7cfda2bbc40 is offered for this system
   1. Disk 1: 8192 MiB, GUID unavailable; eligible: blank disk: no partition table
   Selected target: Disk 1 (8192 MiB), GUID unavailable.
   Journal size (MiB): 60
   ```

   If several disks qualify, it asks for the number of the disk to use. Press
   Enter to accept the journal size.

5. Read the review. It states the revision and warns that the whole disk will
   be rebuilt. Choose **1. Apply**.

   ```text
   ERASE: the entire disk will be rebuilt; every existing partition and volume
   will be destroyed, including any Fedora or Windows installation.
   ...
   1. Apply
   2. Print plan
   3. Dry-run
   4. Cancel
   Review action:
   ```

6. Type `wipe` and press Enter. The installer writes the disk and then
   downloads and installs the programs, listing each one as it goes.

7. When it finishes it prints `installed`:

   ```text
   installed
   Boot record and owned menu activation verified.
   Remove the install medium before the machine restarts.
   1. Restart
   2. Remain here
   ```

   Unplug the stick, then choose **1. Restart**.

The machine restarts into the installed system. Continue with
[first boot and spaces](first-boot.md).

## If the installer stops early

- With no network, or with no programs published for this version of Pyxis, the
  installer refuses before it asks you to confirm anything. It writes nothing
  and returns to the first list:

  ```text
  pkg: repository public: no complete download; active set unchanged
  installer: Install needs base from the repository, and pkg could not confirm it (above); nothing was written
  Returning to installer actions.
  ```

  Check the cable and the repository address, then choose **Install** again.
- Ctrl+C at a prompt, or **Cancel** in a list, stops the installer without
  writing anything as long as you have not yet typed `wipe`:

  ```text
  Cancelled. Nothing was written.
  Returning to installer actions.
  ```

- A failure after `wipe` can leave a disk that does not boot. Run the
  installer again.
