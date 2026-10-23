# Install Pyxis

This page runs the Pyxis installer in QEMU and installs onto a virtual disk.
The installer downloads the programs it installs, so the host needs an
Internet connection. Running on real hardware is not supported yet; see
[real hardware](#real-hardware) at the end.

You need a Linux host with QEMU and UEFI firmware (OVMF) installed.

## Download and verify the ISO

Releases publish the install ISO with a signed checksum list. The examples use
version `0.0.8`; replace it with the release you download. The files are
published with the release, under `https://packages.pyxis.nexus/images/`.

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

4. Save this launcher as `pyxis-qemu.sh`. It starts a four-CPU q35 machine
   with 4 GiB of RAM, UEFI firmware, the virtual disk and a network card that
   reaches the Internet through QEMU's user-mode networking. Give it an ISO to
   install from, or no argument to start the installed disk. Edit the two
   firmware paths at the top if yours differ from the Fedora ones:

   ```sh
   #!/bin/sh
   # Usage: ./pyxis-qemu.sh [pyxis-VERSION.iso]
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

## Install

1. Start the installer from the ISO you verified. Use the path where you
   saved it:

   ```sh
   ./pyxis-qemu.sh ../pyxis-0.0.8.iso
   ```

2. The boot menu lists two entries. Press Down to select **Install Pyxis**,
   then Enter. Do this before the menu's timer runs out, or the first entry
   starts.

   ```text
   Pyxis OS (rescue)
   Install Pyxis
   ```

3. The window shows the kernel log in the **Caelum** tab. The installer runs
   in the **Install** tab, which does not open by itself. Press Super+Right.

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

6. Choose which disks the installer may offer. **Proceed with marked media**
   offers only disks marked as safe to erase. **Read the room** also offers
   blank disks, which is what a new virtual disk is. Type `2` and press Enter.

7. The installer checks the package repository and lists the eligible disks.
   With one disk, it selects it. Check that the size is the disk you made:

   ```text
   1. Disk 1: 16384 MiB, GUID unavailable; eligible: blank disk: no partition table
   Selected target: Disk 1 (16384 MiB), GUID unavailable.
   Journal size (MiB): 60
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

    Choose **2. Remain here**, then close the QEMU window.

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

## Start the installed system

Run the launcher again, this time without an ISO:

```sh
./pyxis-qemu.sh
```

The boot menu starts **Pyxis OS (Caelum)** after three seconds, and the
**Pyxis** tab opens with a shell prompt. Continue with
[first boot and spaces](first-boot.md).

To install again from scratch, delete `pyxis-disk.img` and `pyxis-vars.fd` and
repeat the steps from creating the disk.

## Real hardware

Pyxis has been tested on two machines only: a ThinkPad T14 Gen 1 (AMD) and an
IdeaPad Slim 5 (Intel). Other machines may boot to a black screen or lack
working devices such as the keyboard, network card or storage. Installing on
real hardware is not supported for testers yet. The installer erases the whole
disk it installs on, so do not try it on a machine that holds data you want to
keep.
