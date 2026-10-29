#!/usr/bin/env python3
"""Build and check a release's ready-to-run QEMU image (docs/wip/qemu-image.md).

build runs the release install ISO in QEMU on a blank sparse 32 GiB disk and
answers the real installer's prompts through QEMU's monitor, then converts the
disk to a compressed qcow2. verify checks an image and boots it. Neither needs
root; both need qemu-system-x86_64, qemu-img, OVMF and, by default, /dev/kvm.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

DISK_BYTES = 32 * 1024**3
RAM = "4G"
CPUS = "4"

# Raw .fd firmware pairs by distribution: Fedora, Debian and Ubuntu, Arch, and the
# older Debian path that the Makefile's run target defaults to.
FIRMWARE = (
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
)

# Kernel-log lines that reach the serial file and mark the installer's prompts
# (the prompts themselves are only on the screen). Each is the last thing the
# installer logs before it waits for the next answer.
NETWORK_UP = "eth0: DHCP"
JOURNAL_PROMPT = "installer progress PAUSE Obtaining fresh disk and volume identities:"
REVIEW_READY = "installer progress END Hashing immutable install sources:"
INSTALL_DONE = "installer progress END Opening program volume for cleanup:"
INSTALLED_BOOT = "boot-init: space pyxis started: system://config/init-installed"

# US-layout QMP qcodes for the characters the prompts and checks type.
KEYS = {"\n": ["ret"], " ": ["spc"], "-": ["minus"], ".": ["dot"], "/": ["slash"],
        ":": ["shift", "semicolon"]}


class ImageError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ImageError(message)


def log(message):
    print("qemu-image: " + message, file=sys.stderr, flush=True)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def find_tool(variable, name):
    path = os.environ.get(variable) or shutil.which(name)
    require(path, f"{name} not found; install it or set {variable}")
    return path


def find_firmware():
    code, variables = os.environ.get("OVMF_CODE"), os.environ.get("OVMF_VARS")
    if code or variables:
        require(code and variables, "set both OVMF_CODE and OVMF_VARS, or neither")
        pairs = ((code, variables),)
    else:
        pairs = FIRMWARE
    for pair in pairs:
        if all(os.access(path, os.R_OK) for path in pair):
            return pair
    raise ImageError("no OVMF .fd pair found; set OVMF_CODE and OVMF_VARS")


class Machine:
    """One QEMU process with a serial log and a QMP socket in its own directory."""

    def __init__(self, directory, disk, disk_format, accel, iso=None, no_reboot=False):
        self.directory = directory
        self.serial = directory / "serial.log"
        self.offset = 0
        self.text = ""
        code, variables = find_firmware()
        shutil.copy(variables, directory / "vars.fd")
        command = [
            find_tool("QEMU", "qemu-system-x86_64"),
            "-machine", "q35", "-accel", accel, "-cpu", "max", "-smp", CPUS, "-m", RAM,
            "-rtc", "base=utc",
            "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
            "-drive", "if=pflash,format=raw,unit=1,file=vars.fd",
            "-drive", f"if=none,id=disk,format={disk_format},file={disk},"
                      "discard=unmap,detect-zeroes=unmap",
            "-device", "virtio-blk-pci,drive=disk,disable-legacy=on",
            "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0,disable-legacy=on",
            "-object", "rng-random,id=rng0,filename=/dev/urandom",
            "-device", "virtio-rng-pci,rng=rng0,disable-legacy=on",
            "-display", "none", "-serial", "file:serial.log",
            "-qmp", "unix:qmp.sock,server=on,wait=off",
        ]
        if iso:
            command += ["-cdrom", str(iso), "-boot", "d"]
        if no_reboot:
            command.append("-no-reboot")
        self.process = subprocess.Popen(command, cwd=directory, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE)
        self.qmp = self.connect()

    def connect(self):
        deadline = time.monotonic() + 30
        while True:
            try:
                connection = socket.socket(socket.AF_UNIX)
                # Through the process's cwd, to stay under the 108-byte AF_UNIX path limit.
                connection.connect(f"/proc/{self.process.pid}/cwd/qmp.sock")
                break
            except OSError:
                connection.close()
                require(self.process.poll() is None, self.failure("QEMU exited at start"))
                require(time.monotonic() < deadline, "QEMU's monitor did not come up")
                time.sleep(0.2)
        self.stream = connection.makefile("rw")
        self.stream.readline()
        self.command("qmp_capabilities")
        return connection

    def failure(self, what):
        error = self.process.stderr.read().decode(errors="replace").strip() \
            if self.process.poll() is not None else ""
        return what + (": " + error if error else "")

    def command(self, name, **arguments):
        self.stream.write(json.dumps({"execute": name, "arguments": arguments}) + "\n")
        self.stream.flush()
        while True:
            reply = json.loads(self.stream.readline())
            if "return" in reply:
                return reply["return"]
            if "error" in reply:
                raise ImageError(f"QEMU {name}: {reply['error']['desc']}")

    def type(self, text, pause=0.08):
        for character in text:
            names = KEYS.get(character, [character])
            self.command("send-key", keys=[{"type": "qcode", "data": name} for name in names])
            time.sleep(pause)

    def screenshot(self, path):
        self.command("screendump", filename=str(path), format="png")

    def mark(self):
        self.read()
        return len(self.text)

    def read(self):
        if self.serial.exists():
            with open(self.serial, "rb") as stream:
                stream.seek(self.offset)
                data = stream.read()
            self.offset += len(data)
            self.text += data.decode(errors="replace")

    def wait(self, marker, since, timeout, what):
        """Wait for a serial line containing marker, written after offset since."""
        deadline = time.monotonic() + timeout
        while True:
            self.read()
            at = self.text.find(marker, since)
            if at >= 0:
                return at + len(marker)
            require(self.process.poll() is None, self.failure(f"QEMU exited while waiting for {what}"))
            require(time.monotonic() < deadline, f"timed out waiting for {what}")
            time.sleep(0.5)

    def wait_exit(self, timeout, what):
        try:
            self.process.wait(timeout)
        except subprocess.TimeoutExpired:
            raise ImageError(f"timed out waiting for {what}") from None

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.stream.close()
        self.qmp.close()


def accelerator(arguments):
    if arguments.accel == "kvm":
        require(os.access("/dev/kvm", os.R_OK | os.W_OK),
                "/dev/kvm is not usable; fix access or pass --accel tcg")
    return arguments.accel


def drive_installer(machine, timeout):
    """Answer the installer's prompts on a blank disk: Install, Classic, Blank or
    marked disks, the default journal, Apply, wipe, Restart."""
    machine.wait(NETWORK_UP, 0, timeout, "the install network")
    time.sleep(3)
    machine.type("1\n")
    time.sleep(2)
    machine.type("2\n")
    time.sleep(2)
    since = machine.mark()
    machine.type("\n")
    machine.wait(JOURNAL_PROMPT, since, timeout, "the journal prompt (does pkg reach the repository?)")
    time.sleep(1)
    since = machine.mark()
    machine.type("\n")
    machine.wait(REVIEW_READY, since, timeout, "the review")
    time.sleep(2)
    machine.type("1\n")
    time.sleep(3)
    since = machine.mark()
    machine.type("wipe\n")
    machine.wait(INSTALL_DONE, since, timeout, "the end of the install")
    time.sleep(5)
    machine.type("\n")
    machine.wait_exit(120, "QEMU to exit after Restart")


def build(arguments):
    iso = Path(arguments.iso)
    output = Path(arguments.output)
    qemu_img = find_tool("QEMU_IMG", "qemu-img")
    accel = accelerator(arguments)
    require(iso.is_file(), f"{iso} is not a file")
    require(not output.exists(), f"{output} exists; a published image is never rebuilt in place")
    digest = sha256_file(iso)
    sums = Path(arguments.sums).read_text().splitlines()
    require(f"{digest}  {iso.name}" in sums, f"{iso.name} does not match its line in {arguments.sums}")
    log(f"building from {iso.name} {digest}")

    with tempfile.TemporaryDirectory(prefix="pyxis-image-") as work:
        work = Path(work)
        disk = work / "disk.raw"
        with open(disk, "wb") as stream:
            stream.truncate(DISK_BYTES)
        started = time.monotonic()
        machine = Machine(work, disk, "raw", accel, iso=iso, no_reboot=True)
        try:
            drive_installer(machine, arguments.timeout)
        finally:
            machine.close()
        log(f"installer finished in {time.monotonic() - started:.0f} s")
        temporary = output.with_name(output.name + ".tmp")
        try:
            subprocess.run([qemu_img, "convert", "-c", "-O", "qcow2", str(disk), str(temporary)],
                           check=True)
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
    try:
        verify_image(output, accel, Path(arguments.screens) if arguments.screens else None,
                     arguments.timeout)
    except (ImageError, subprocess.CalledProcessError):
        output.unlink()
        raise
    log(f"built {output}: sha256 {sha256_file(output)}, {output.stat().st_size} bytes")


def info(qemu_img, image):
    report = subprocess.run([qemu_img, "info", "--output=json", str(image)], check=True,
                            capture_output=True, text=True).stdout
    return json.loads(report)


def verify_image(image, accel, screens, timeout):
    qemu_img = find_tool("QEMU_IMG", "qemu-img")
    require(image.is_file(), f"{image} is not a file")
    report = info(qemu_img, image)
    require(report["format"] == "qcow2", f"{image} is {report['format']}, not qcow2")
    require(report["virtual-size"] == DISK_BYTES, f"{image} is not {DISK_BYTES // 1024**3} GiB")
    require("backing-filename" not in report, f"{image} has a backing file")
    subprocess.run([qemu_img, "check", str(image)], check=True, stdout=subprocess.DEVNULL)
    log(f"qcow2, {report['virtual-size'] // 1024**3} GiB virtual, {report['actual-size']} bytes allocated")

    # Boot twice on one overlay: the first boot mutates the disk, the second shows a
    # recovered pool. The image itself is never written.
    with tempfile.TemporaryDirectory(prefix="pyxis-verify-") as work:
        work = Path(work)
        overlay = work / "overlay.qcow2"
        subprocess.run([qemu_img, "create", "-q", "-f", "qcow2", "-b", str(image.resolve()),
                        "-F", "qcow2", str(overlay)], check=True)
        for boot in (1, 2):
            machine = Machine(work, overlay, "qcow2", accel)
            try:
                machine.wait(INSTALLED_BOOT, 0, timeout, f"the installed system (boot {boot})")
                machine.wait(NETWORK_UP, 0, timeout, f"the network (boot {boot})")
                # The Pyxis tab takes over by itself unless a key arrives first.
                time.sleep(15)
                if screens:
                    machine.type("cat boot://os-release\n")
                    time.sleep(3)
                    machine.type("pkg upgrade\n")
                    time.sleep(15)
                    screens.mkdir(parents=True, exist_ok=True)
                    machine.screenshot(screens / f"boot{boot}.png")
                    log(f"boot {boot}: screenshot {screens / f'boot{boot}.png'}")
                else:
                    log(f"boot {boot}: installed system started")
            finally:
                machine.close()
            os.replace(work / "serial.log", work / f"serial{boot}.log")


def verify(arguments):
    verify_image(Path(arguments.image), accelerator(arguments),
                 Path(arguments.screens) if arguments.screens else None, arguments.timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("build", "verify"):
        command = commands.add_parser(name)
        if name == "build":
            command.add_argument("iso")
            command.add_argument("output")
            command.add_argument("--sums", required=True,
                                 help="the SHA256SUMS that lists the ISO; the build refuses a mismatch")
        else:
            command.add_argument("image")
        command.add_argument("--accel", choices=("kvm", "tcg"), default="kvm")
        command.add_argument("--timeout", type=int, default=300,
                             help="seconds to wait for each step (raise it for tcg)")
        command.add_argument("--screens", help="directory for screenshots of the shell's "
                             "`cat boot://os-release` and `pkg upgrade`, for a person to read")
    arguments = parser.parse_args()
    try:
        {"build": build, "verify": verify}[arguments.command](arguments)
    except (ImageError, subprocess.CalledProcessError) as error:
        print(f"qemu-image: {error}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
