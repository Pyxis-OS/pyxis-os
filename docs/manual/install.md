# Install Pyxis

This page runs the Pyxis installer in QEMU and installs onto a virtual disk.
The installer downloads the programs it installs, so the host needs an
Internet connection. From 0.0.10 each release also publishes a
[ready-made image](#run-the-ready-made-image) with Pyxis already installed, which
skips the installer. Running on real hardware is not supported yet; see
[real hardware](#real-hardware) at the end.

You need a Linux host with QEMU and UEFI firmware (OVMF) installed.

## Download and verify the ISO

Releases publish the install ISO with a signed checksum list. The examples use
version `0.0.8`; replace it with the release you download. The files are
published with the release, under `https://packages.pyxis.nexus/images/`. The
same steps verify the [ready-made image](#run-the-ready-made-image),
`pyxis-VERSION.qcow2`: download it in step 1 and expect `pyxis-VERSION.qcow2: OK`
in step 5.

1. Download the ISO, the checksum list, its signature, the signer certificate
   and the signing CA certificate:

   ```sh
   base=https://packages.pyxis.nexus/images
   curl -fLO "$base/pyxis-0.0.8.iso"
   curl -fLO "$base/SHA256SUMS"
   curl -fLO "$base/SHA256SUMS.sig"
   curl -fLO "$base/signer-2026.pem"
   curl -fLO "$base/pyxis-signing-ca.pem"
   ```

2. Check that the CA certificate is the Pyxis Software Signing CA, by
   comparing its fingerprint with the one printed here:

   ```sh
   openssl x509 -in pyxis-signing-ca.pem -noout -subject -fingerprint -sha256
   ```

   ```text
   subject=CN=Pyxis Software Signing CA
   sha256 Fingerprint=D5:18:25:8B:27:E6:A2:B0:D7:81:A0:35:BE:E5:D3:2C:7F:C2:38:97:84:26:98:25:30:39:7E:64:3A:91:69:73
   ```

   If the fingerprint differs, stop and do not use the files.

3. Check that the signer certificate was issued by that CA:

   ```sh
   openssl verify -x509_strict -partial_chain -purpose codesign \
     -CAfile pyxis-signing-ca.pem signer-2026.pem
   ```

   ```text
   signer-2026.pem: OK
   ```

4. Check the signature on the checksum list:

   ```sh
   openssl x509 -in signer-2026.pem -noout -pubkey > signer.pub
   openssl dgst -sha256 -verify signer.pub -signature SHA256SUMS.sig SHA256SUMS
   ```

   ```text
   Verified OK
   ```

5. Check the ISO against the list:

   ```sh
   sha256sum -c --ignore-missing SHA256SUMS
   ```

   ```text
   pyxis-0.0.8.iso: OK
   ```

Continue only when every step printed the result shown.

## Set up QEMU

1. Install QEMU and the UEFI firmware. The package names are usually:

   | Distribution | Packages |
   | --- | --- |
   | Fedora | `qemu-system-x86`, `qemu-img`, `edk2-ovmf` |
   | Debian, Ubuntu | `qemu-system-x86`, `qemu-utils`, `ovmf` |
   | Arch | `qemu-system-x86`, `qemu-img`, `edk2-ovmf` |

2. Find the firmware pair, a `CODE` file and a `VARS` file of the same
   family. Common locations:

   | Distribution | `OVMF_CODE` | `OVMF_VARS` |
   | --- | --- | --- |
   | Fedora | `/usr/share/edk2/ovmf/OVMF_CODE.fd` | `/usr/share/edk2/ovmf/OVMF_VARS.fd` |
   | Debian, Ubuntu | `/usr/share/OVMF/OVMF_CODE_4M.fd` | `/usr/share/OVMF/OVMF_VARS_4M.fd` |
   | Arch | `/usr/share/edk2/x64/OVMF_CODE.4m.fd` | `/usr/share/edk2/x64/OVMF_VARS.4m.fd` |

   If a path does not exist on your system, search for the files:

   ```sh
   find /usr/share -name 'OVMF_*.fd' 2>/dev/null
   ```

3. Make a folder for the virtual machine and create its disk. The file is
   sparse, so it takes space only as the installer writes to it:

   ```sh
   mkdir pyxis-vm && cd pyxis-vm
   qemu-img create -f raw pyxis-disk.img 16G
   ```

4. Check that your user may use KVM, QEMU's hardware acceleration. Add
   yourself to the `kvm` group, then log out and back in, or reboot, before you
   run QEMU:

   ```sh
   sudo usermod -aG kvm "$USER"
   ```

   Run QEMU as your normal user, never with `sudo` or as root. A root program
   with a window in your desktop session gets unreliable keyboard focus and
   grabs, and it leaves root-owned `pyxis-vars.fd` and `pyxis-disk.img` files
   that your own user can no longer write.

5. Save this launcher as `pyxis-qemu.sh`. It starts a four-CPU q35 machine
   with 4 GiB of RAM, UEFI firmware, the virtual disk and a network card that
   reaches the Internet through QEMU's user-mode networking. Give it an ISO to
   install from, or no argument to start the installed disk. Edit the two
   firmware paths at the top if yours differ from the Fedora ones:

   ```sh
   #!/bin/sh
   # Usage: ./pyxis-qemu.sh [pyxis-VERSION.iso]
   if [ -z "$1" ]; then
     echo 'Usage: ./pyxis-qemu.sh [pyxis-VERSION.iso] - pass the ISO to install' >&2
   fi
   OVMF_CODE=${OVMF_CODE:-/usr/share/edk2/ovmf/OVMF_CODE.fd}
   OVMF_VARS=${OVMF_VARS:-/usr/share/edk2/ovmf/OVMF_VARS.fd}

   [ -f pyxis-vars.fd ] || cp "$OVMF_VARS" pyxis-vars.fd
   if [ -n "$1" ]; then
     set -- -cdrom "$1" -boot d
   fi

   exec qemu-system-x86_64 \
     -machine q35 -accel kvm -cpu max -smp 4 -m 4G -rtc base=utc \
     -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
     -drive if=pflash,format=raw,unit=1,file=pyxis-vars.fd \
     -drive if=none,id=disk,format=raw,file=pyxis-disk.img \
     -device virtio-blk-pci,drive=disk,disable-legacy=on \
     -netdev user,id=net0 \
     -device virtio-net-pci,netdev=net0,disable-legacy=on \
     -object rng-random,id=rng0,filename=/dev/urandom \
     -device virtio-rng-pci,rng=rng0,disable-legacy=on \
     -display gtk \
     "$@"
   ```

   ```sh
   chmod +x pyxis-qemu.sh
   ```

   `-accel kvm` needs access to `/dev/kvm`. Without KVM, use `-accel tcg`.
   Without an ISO, the script prints its usage line to the error output and
   still starts the installed disk, so run it with the ISO for the first
   install and without one afterwards.

   **Keyboard and mouse.** On a Wayland desktop, click into QEMU's window and
   press Ctrl+Alt+G to grab the keyboard and mouse before you type; press it
   again to release them. Without the grab the mouse pointer drifts out of step
   with the guest's, because Pyxis reads only relative pointer movement and has
   no absolute pointer (tablet) support yet. If the GTK window gives you trouble,
   replace `-display gtk` with `-display sdl`.

## Install

1. Start the installer from the ISO you verified. Use the path where you
   saved it:

   ```sh
   ./pyxis-qemu.sh ../pyxis-0.0.8.iso
   ```

2. The boot menu lists two entries. **Install Pyxis** is first and starts by
   itself after five seconds, so leave it alone.

   ```text
   Install Pyxis
   Pyxis OS (rescue)
   ```

3. The window first shows the kernel log in the **Caelum** tab. If it still
   does, wait a moment: the **Install** tab opens by itself and the installer
   starts the network and shows its first list:

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

   If your desktop takes a Super key shortcut, grab the keyboard first, as
   described under [Set up QEMU](#set-up-qemu).

4. Choose **1. Install**.

5. Choose the layout. **Classic** uses the whole disk. **Coexist** keeps other
   partitions and is described in [native installation](../userland/installer.md).
   Type `2` and press Enter.

   ```text
   1. Coexist
   2. Classic
   3. Cancel
   Choose installation layout
   ```

6. Choose which disks the installer may offer. **Blank or marked disks** is the
   default and includes a new, empty virtual disk, so press Enter.
   **Any disk (may erase data)** also offers disks that hold other data,
   including an earlier Pyxis installation.

   ```text
   1. Blank or marked disks
   2. Any disk (may erase data)
   3. Cancel
   Install consent:
   ```

7. The installer checks the package repository and lists the eligible disks.
   With one disk, it selects it. Check that the size is the disk you made:

   ```text
   1. Disk 1: 16384 MiB, GUID unavailable; eligible: blank disk: no partition table
   Selected target: Disk 1 (16384 MiB), GUID unavailable.
   Journal size (MiB): 124
   ```

   Press Enter to accept the journal size.

8. Read the review. It states the revision and warns that the whole disk will
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

9. Type `wipe` and press Enter. The installer writes the disk, then downloads
   and installs the programs, listing each one as it goes.

10. When it finishes it prints `installed`:

    ```text
    installed
    Boot record and owned menu activation verified.
    Remove the install medium before the machine restarts.
    1. Restart
    2. Remain here
    ```

    Choose **2. Remain here**, which prints `Not restarting.`, then close the
    QEMU window.

### If the installer stops early

- With no network, or with no programs published for this version of Pyxis, the
  installer refuses before it asks you to confirm anything. It writes nothing
  and returns to the first list:

  ```text
  pkg: repository public: no complete download; active set unchanged
  installer: Install needs base from the repository, and pkg could not confirm it (above); nothing was written
  Returning to installer actions.
  ```

  Check the host's Internet connection, then choose **Install** again.
- Ctrl+C at a prompt, or **Cancel** in a list, stops the installer without
  writing anything as long as you have not yet typed `wipe`:

  ```text
  Cancelled. Nothing was written.
  Returning to installer actions.
  ```

- A failure after `wipe` can leave a disk that does not boot. Run the
  installer again.

### Install again over Pyxis

A disk that already holds a Pyxis installation is not offered by the default
**Blank or marked disks**, because a finished installation is not marked as safe
to erase. The installer says so and writes nothing:

```text
1. Disk 1: 16384 MiB, GUID ...; ineligible: npfs pool, partially marked: some live volumes lack a regular SAFE_TO_WIPE; choose
Any disk to erase it
No eligible disks. Nothing was written.
Returning to installer actions.
```

To reinstall over it, choose **2. Any disk (may erase data)** at the same step.
The disk is then offered and the install continues from the journal size, as
above.

## Start the installed system

Run the launcher again, this time without an ISO:

```sh
./pyxis-qemu.sh
```

The boot menu starts **Pyxis OS (Caelum)** after three seconds, and the
**Pyxis** tab opens with a shell prompt. Continue with
[first boot and spaces](first-boot.md).

To start over with an empty disk instead, delete `pyxis-disk.img` and
`pyxis-vars.fd` and repeat the steps from creating the disk.

## Run the ready-made image

From 0.0.10 each release publishes `pyxis-VERSION.qcow2` beside the ISO: a disk
with that release installed the way the installer does it, so `pkg upgrade` works
on it. It starts from the boot menu to a shell without installing. It holds the
base programs only; add others with `pkg install`. Every download is the same
disk. The disk is 32 GiB, but the file is only tens of MiB and grows only as the
virtual machine writes to it.

1. Download and verify `pyxis-VERSION.qcow2` as described in
   [Download and verify the ISO](#download-and-verify-the-iso). Check it before
   the first boot: running it changes the file, so it no longer matches its
   checksum afterwards.
2. Install QEMU and the UEFI firmware as in [Set up QEMU](#set-up-qemu), steps 1
   and 2. Add yourself to the `kvm` group as in step 4, and run QEMU as your
   normal user; the launcher below still works without KVM, only much slower.
3. Save this launcher as `pyxis-image.sh`. It starts a four-CPU q35 machine with
   4 GiB of RAM, UEFI firmware, the image as a virtual disk and a network card
   that reaches the Internet through QEMU's user-mode networking. It uses the
   first firmware pair it finds in the locations below; set `OVMF_CODE` and
   `OVMF_VARS` to override them. Firmware paths: Fedora
   `/usr/share/edk2/ovmf/OVMF_CODE.fd` and `OVMF_VARS.fd`; Debian and Ubuntu
   `/usr/share/OVMF/OVMF_CODE_4M.fd` and `OVMF_VARS_4M.fd`; Arch
   `/usr/share/edk2/x64/OVMF_CODE.4m.fd` and `OVMF_VARS.4m.fd`.

   ```sh
   #!/bin/sh
   # Usage: ./pyxis-image.sh pyxis-VERSION.qcow2 [more QEMU options]
   image=${1:?usage: ./pyxis-image.sh pyxis-VERSION.qcow2 [more QEMU options]}
   shift

   # Use the first firmware pair that exists, or set OVMF_CODE and OVMF_VARS.
   if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS" ]; then
     for pair in \
       /usr/share/edk2/ovmf/OVMF_CODE.fd:/usr/share/edk2/ovmf/OVMF_VARS.fd \
       /usr/share/OVMF/OVMF_CODE_4M.fd:/usr/share/OVMF/OVMF_VARS_4M.fd \
       /usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd
     do
       if [ -r "${pair%%:*}" ] && [ -r "${pair#*:}" ]; then
         OVMF_CODE=${pair%%:*}
         OVMF_VARS=${pair#*:}
         break
       fi
     done
   fi
   if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS" ]; then
     echo 'No OVMF firmware found: find it with  find /usr/share -name "OVMF_*.fd"  and set OVMF_CODE and OVMF_VARS.' >&2
     exit 1
   fi

   # KVM needs access to /dev/kvm; without it QEMU emulates the CPU, which is slow.
   if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
     accel=kvm
   else
     accel=tcg
     echo 'No access to /dev/kvm: running without KVM, which is much slower.' >&2
   fi

   vars=${image%.qcow2}-vars.fd
   [ -f "$vars" ] || cp "$OVMF_VARS" "$vars"

   exec qemu-system-x86_64 \
     -machine q35 -accel "$accel" -cpu max -smp 4 -m 4G -rtc base=utc \
     -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE" \
     -drive if=pflash,format=raw,unit=1,file="$vars" \
     -drive if=none,id=disk,format=qcow2,file="$image" \
     -device virtio-blk-pci,drive=disk,disable-legacy=on \
     -netdev user,id=net0 \
     -device virtio-net-pci,netdev=net0,disable-legacy=on \
     -object rng-random,id=rng0,filename=/dev/urandom \
     -device virtio-rng-pci,rng=rng0,disable-legacy=on \
     -display gtk \
     "$@"
   ```

   ```sh
   chmod +x pyxis-image.sh
   ```

4. Start the image:

   ```sh
   ./pyxis-image.sh pyxis-0.0.10.qcow2
   ```

   The boot menu starts **Pyxis OS (Caelum)** after three seconds and the
   **Pyxis** tab opens with a shell prompt. Continue with
   [first boot and spaces](first-boot.md). The keyboard and mouse notes under
   [Set up QEMU](#set-up-qemu) apply here too.

The image keeps your files in `home://` between runs. To try it without changing
the file, add `-snapshot`: `./pyxis-image.sh pyxis-0.0.10.qcow2 -snapshot`; the
changes are discarded when QEMU exits. Converting the image to a raw file, or
copying it with a tool that does not keep holes, makes a full 32 GiB file.

## Real hardware

Pyxis has been tested on two machines only: a ThinkPad T14 Gen 1 (AMD) and an
IdeaPad Slim 5 (Intel). Other machines may boot to a black screen or lack
working devices such as the keyboard, network card or storage. Installing on
real hardware is not supported for testers yet. The installer erases the whole
disk it installs on, so do not try it on a machine that holds data you want to
keep.
