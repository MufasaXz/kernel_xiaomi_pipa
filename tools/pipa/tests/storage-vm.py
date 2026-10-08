#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Boot storage tests using a new, disposable virtual disk on every run."""
import argparse
import gzip
import pathlib
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--kernel", type=pathlib.Path, required=True)
parser.add_argument("--results", type=pathlib.Path, required=True)
args = parser.parse_args()
args.kernel = args.kernel.resolve(strict=True)
args.results = args.results.resolve()
args.results.mkdir(parents=True, exist_ok=True)
here = pathlib.Path(__file__).resolve().parent

def run(*command, **kwargs):
    return subprocess.run(command, check=True, **kwargs)

with tempfile.TemporaryDirectory(prefix="pipa-storage-vm-") as work:
    work = pathlib.Path(work)
    root = work / "root"
    root.mkdir()
    for directory in ("bin", "sbin", "dev", "proc", "sys", "mnt", "tmp", "run"):
        (root / directory).mkdir()

    def copy_binary(name, destination):
        binary = pathlib.Path(shutil.which(name) or name)
        target = root / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(binary, target)
        deps = subprocess.run(["ldd", str(binary)], capture_output=True, text=True)
        for dependency in re.findall(r"(/[^\s()]+)", deps.stdout):
            source = pathlib.Path(dependency)
            if source.is_file():
                target = root / source.relative_to("/")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)

    copy_binary("busybox", "bin/busybox")
    applets = subprocess.check_output(["busybox", "--list"], text=True).splitlines()
    for applet in applets:
        if applet != "busybox":
            (root / "bin" / applet).symlink_to("busybox")
    for name in ("dmsetup", "mkfs.ext4", "mkfs.f2fs"):
        copy_binary(name, "sbin/" + name)
    shutil.copy2(here / "storage-init.sh", root / "init")
    (root / "init").chmod(0o755)
    run("gcc", "-static", "-O2", "-Wall", "-Wextra", str(here / "fscrypt-roundtrip.c"),
        "-o", str(root / "bin/fscrypt-roundtrip"))
    run("gcc", "-static", "-O2", "-Wall", "-Wextra", "-Werror", str(here / "memfd-compat.c"),
        "-o", str(root / "bin/memfd-compat"))
    entries = sorted(str(p.relative_to(root)) for p in root.rglob("*"))
    packed = run("cpio", "--null", "-o", "--format=newc", "--owner=0:0", cwd=root,
                 input=(chr(0).join(entries) + chr(0)).encode(), capture_output=True)
    initramfs = work / "initramfs.gz"
    initramfs.write_bytes(gzip.compress(packed.stdout, mtime=0))
    disk = work / "scratch.raw"
    with disk.open("wb") as stream:
        stream.truncate(256 * 1024 * 1024)
    command = ["qemu-system-x86_64", "-machine", "q35,accel=tcg", "-cpu", "max",
               "-m", "512", "-smp", "2", "-nographic", "-no-reboot", "-nic", "none",
               "-kernel", str(args.kernel), "-initrd", str(initramfs),
               "-append", "console=ttyS0 rdinit=/init panic=1",
               "-drive", "file=" + str(disk) + ",format=raw,if=virtio"]
    log = args.results / "storage-vm.log"
    with log.open("wb") as stream:
        try:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=240)
        except subprocess.TimeoutExpired:
            raise SystemExit("Storage VM timed out; inspect " + str(log))
    output = log.read_text(errors="replace")
    print(chr(10).join(line for line in output.splitlines()
                      if "PASS" in line or "FAIL" in line or "fscrypt " in line))
    if result.returncode or "PIPA_STORAGE_TEST_PASSED" not in output:
        raise SystemExit("Storage test failed; inspect " + str(log))
    print("Storage VM passed; full log: " + str(log))
