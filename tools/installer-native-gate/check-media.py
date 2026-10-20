#!/usr/bin/env python3
"""Read-only identity snapshot for the owner-approved green 64 GB reference USB disk.

This neither grants authority nor performs device writes, mounts or unmounts.
The installer must independently review the current device immediately before use.
"""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys


def refuse(reason):
    raise ValueError(reason)


def run(*arguments):
    result = subprocess.run(arguments, check=True, text=True, capture_output=True,
                            timeout=15)
    return result.stdout.strip()


def descendants(node):
    yield node
    for child in node.get("children", []):
        yield from descendants(child)


def single_usb_disk():
    data = json.loads(run("lsblk", "--json", "--bytes", "--paths", "--nodeps",
                          "--output", "NAME,TYPE,SIZE,TRAN"))
    disks = [disk for disk in data.get("blockdevices", [])
             if disk.get("type") == "disk" and disk.get("tran") == "usb"]
    if len(disks) != 1:
        refuse(f"require exactly one USB whole disk; found {len(disks)}")
    return disks[0]


def discover():
    disk = single_usb_disk()
    size = int(disk["size"])
    if not 60_000_000_000 <= size <= 66_000_000_000:
        refuse("the only USB whole disk is not in the 64 GB class; refuse 32/128 GB media")
    device = Path(disk["name"]).resolve(strict=True)
    aliases = sorted(alias for alias in Path("/dev/disk/by-id").glob("usb-*")
                     if alias.is_symlink() and not re.search(r"-part[0-9]+$", alias.name)
                     and alias.resolve(strict=True) == device)
    if not aliases:
        refuse("the only USB whole disk has no USB whole-device by-id")
    return str(aliases[0]), size


def check(arguments):
    if not 60_000_000_000 <= arguments.expected_bytes <= 66_000_000_000:
        refuse("only the green 64 GB reference stick is approved; refuse 32 GB durability and blue 128 GB personal media")
    only_disk = single_usb_disk()
    alias = Path(arguments.device)
    if not alias.is_absolute() or alias.parent != Path("/dev/disk/by-id"):
        refuse("require an explicit absolute /dev/disk/by-id whole-device path")
    if not alias.is_symlink() or re.search(r"-part[0-9]+$", alias.name):
        refuse("require an existing whole-device by-id symlink, never a partition")
    device = alias.resolve(strict=True)
    info = device.stat()
    if not stat.S_ISBLK(info.st_mode):
        refuse("the by-id target is not a block device")
    if device != Path(only_disk["name"]).resolve(strict=True):
        refuse("recorded by-id does not identify the only USB whole disk")
    before = (device, info.st_rdev)
    data = json.loads(run("lsblk", "--json", "--bytes", "--paths", "--output",
                          "NAME,TYPE,SIZE,RO,TRAN,VENDOR,MODEL,MOUNTPOINTS,PKNAME,MAJ:MIN",
                          str(device)))
    disks = data.get("blockdevices", [])
    if len(disks) != 1 or disks[0]["type"] != "disk" or disks[0].get("pkname"):
        refuse("the target is not a whole disk")
    disk = disks[0]
    nodes = list(descendants(disk))
    if disk.get("tran") != "usb":
        refuse("the target is not a USB device")
    sysfs = (Path("/sys/class/block") / device.name).resolve(strict=True)
    usb = None
    for parent in [sysfs, *sysfs.parents]:
        vendor = parent / "idVendor"
        if vendor.is_file():
            usb = parent
            product = parent / "idProduct"
            if not product.is_file():
                refuse("cannot establish USB product identity")
            break
    if usb is None:
        refuse("cannot independently establish USB ancestry")
    description = " ".join(str(disk.get(key) or "") for key in ("vendor", "model"))
    size = int(run("blockdev", "--getsize64", str(device)))
    if size != arguments.expected_bytes or size != int(disk["size"]):
        refuse("size does not match the recorded exact byte count")
    if not 60_000_000_000 <= size <= 66_000_000_000:
        refuse("actual capacity is outside the green 64 GB reference range; never use the blue 128 GB stick")
    nominal = "64 GB reference; exactly one USB whole disk present"
    if int(run("blockdev", "--getro", str(device))) or any(node.get("ro") for node in nodes):
        refuse("device or a descendant is read-only")
    majors = {str(node["maj:min"]) for node in nodes}
    mounted = {line.split()[2] for line in Path("/proc/self/mountinfo").read_text().splitlines()}
    if majors & mounted or any(any(point for point in (node.get("mountpoints") or [])) for node in nodes):
        refuse("device or a descendant is mounted; do not unmount another task's media")
    for line in Path("/proc/swaps").read_text().splitlines()[1:]:
        name = line.split()[0]
        try:
            swap = Path(name).stat()
        except OSError:
            refuse("cannot establish the backing of active swap")
        if stat.S_ISBLK(swap.st_mode) and f"{os.major(swap.st_rdev)}:{os.minor(swap.st_rdev)}" in majors:
            refuse("device or a descendant is active swap")
    for node in nodes:
        holders = Path("/sys/class/block") / Path(node["name"]).name / "holders"
        if holders.is_dir() and any(holders.iterdir()):
            refuse("device or a descendant has an active device-mapper/RAID holder")
    final_disk = single_usb_disk()
    if Path(final_disk["name"]).resolve(strict=True) != device or int(final_disk["size"]) != size:
        refuse("USB whole-disk inventory changed during inspection")
    after = alias.resolve(strict=True)
    if (after, after.stat().st_rdev) != before:
        refuse("by-id backing changed during inspection")
    print("MEDIA CHECK PASS — read-only snapshot; no device writes performed")
    print("By-id: checked privately; raw identifier omitted from reports")
    print(f"Device: {device}  Model: {description.strip()}")
    print(f"Size: {size} bytes  Nominal: {nominal}")
    print(f"State: USB, writable, unmounted, no swap/holders; {len(nodes)-1} descendant(s)")
    print("Target: sole USB whole disk; retain this exact by-id and byte count for installer Review.")
    print("Never select internal NVMe. This check is not a reservation or a destructive command.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="explicit /dev/disk/by-id whole-device symlink")
    parser.add_argument("--expected-bytes", required=True, type=int,
                        help="recorded exact byte count, rechecked with blockdev --getsize64")
    arguments = parser.parse_args()
    try:
        check(arguments)
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, TypeError) as error:
        message = re.sub(r"/dev/disk/by-id/[^\s'\"),]+", "[USB identity redacted]", str(error))
        print("MEDIA CHECK REFUSED: " + message, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
