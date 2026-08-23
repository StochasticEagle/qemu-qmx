#!/usr/bin/env python3
"""Launch-level QMX regression tests for the 0.9 beta hardening pass.

Usage:
    python tests/qmx/test-qmx-integration.py [build/qemu-system-x86_64]

The tests start QEMU headless long enough to establish that machine creation
succeeded, then terminate it.  No guest operating system is required.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[2]
QEMU = (
    pathlib.Path(sys.argv[1]).resolve()
    if len(sys.argv) > 1
    else (ROOT / "build/qemu-system-x86_64").resolve()
)
STARTUP_SECONDS = 1.0


BASE = '''\
qmx = 1
machine = pc
memory = 64M
accel = tcg
display = none
parallel = none
monitor = none
'''


def write_qmx(path: pathlib.Path, body: str) -> None:
    path.write_text(BASE + body, encoding="utf-8")


def launch(args: list[str], cwd: pathlib.Path) -> tuple[str, str]:
    proc = subprocess.Popen(
        [str(QEMU), *args],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        time.sleep(STARTUP_SECONDS)
        rc = proc.poll()
        if rc is not None:
            stdout, stderr = proc.communicate()
            raise AssertionError(
                f"QEMU exited during startup with {rc}\n"
                f"command: {args!r}\nstdout:\n{stdout}\nstderr:\n{stderr}"
            )
        proc.terminate()
        try:
            stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate(timeout=5)
        return stdout, stderr
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)


def test_missing_media(root: pathlib.Path) -> None:
    case = root / "missing-media"
    case.mkdir()
    qmx = case / "machine.qmx"
    write_qmx(
        qmx,
        '''\
serial = none
drive.floppy = file="missing-floppy.img",format=raw,if=floppy
drive.hd = file="missing-hd.qcow2",format=qcow2,if=none
device.hdd = ide-hd,drive=hd,bus=ide.0,unit=0
drive.cd = file="missing-cd.iso",format=raw,media=cdrom,if=none,readonly=on
device.cdrom = ide-cd,drive=cd,bus=ide.1,unit=0
''',
    )
    _, stderr = launch(["-qmx", str(qmx)], root)
    for drive in ("floppy", "hd", "cd"):
        if f"QMX drive '{drive}'" not in stderr:
            raise AssertionError(f"missing-media: no warning for {drive}\n{stderr}")
    for device in ("hdd", "cdrom"):
        if f"QMX device '{device}'" not in stderr:
            raise AssertionError(f"missing-media: dependent {device} not omitted\n{stderr}")


def test_relative_paths(root: pathlib.Path) -> None:
    case = root / "relative-paths"
    run = root / "different-cwd"
    (case / "media").mkdir(parents=True)
    (case / "data").mkdir()
    (case / "logs").mkdir()
    (case / "state").mkdir()
    run.mkdir()

    (case / "media" / "disk.raw").write_bytes(b"\0" * (1024 * 1024))
    (case / "data" / "fw.bin").write_bytes(b"qmx-fw-cfg")
    (case / "data" / "secret.bin").write_bytes(b"qmx-secret")

    qmx = case / "machine.qmx"
    write_qmx(
        qmx,
        '''\
drive.disk = file="media/disk.raw",format=raw,if=none
fw_cfg.data = name="opt/qmx/path-test",file="data/fw.bin"
chardev.log = file,path="logs/chardev.log"
object.secret = secret,file="data/secret.bin"
serial = file:logs/serial.log
nvram = file="state/machine.cmos",format=cmos128,rtc_init=time0
''',
    )
    launch(["-qmx", str(qmx)], run)

    required = (
        case / "logs" / "chardev.log",
        case / "logs" / "serial.log",
        case / "state" / "machine.cmos",
    )
    for path in required:
        if not path.exists():
            raise AssertionError(f"relative-paths: expected QMX-relative path {path}")
    if (run / "state" / "machine.cmos").exists():
        raise AssertionError("relative-paths: NVRAM was incorrectly created relative to cwd")


def test_cli_precedence(root: pathlib.Path) -> None:
    case = root / "precedence"
    case.mkdir()
    (case / "qmx.raw").write_bytes(b"\0" * (1024 * 1024))
    (case / "cli.raw").write_bytes(b"\0" * (1024 * 1024))
    qmx = case / "machine.qmx"
    write_qmx(
        qmx,
        '''\
serial = none
drive.disk = file="qmx.raw",format=raw,if=none
netdev.net0 = user
device.net = e1000,netdev=net0
audiodev.audio0 = none
chardev.char0 = null
object.mem0 = memory-backend-ram,size=1M
fw_cfg.test = name="opt/qmx/precedence",string="qmx"
''',
    )

    launch(
        [
            "-qmx", str(qmx),
            "-drive", f"file={case / 'cli.raw'},format=raw,if=none,id=disk",
            "-netdev", "user,id=net0",
            "-device", "e1000,netdev=net0,id=net",
            "-audiodev", "none,id=audio0",
            "-chardev", "null,id=char0",
            "-object", "memory-backend-ram,size=2M,id=mem0",
            "-fw_cfg", "name=opt/qmx/precedence,string=cli",
        ],
        root,
    )


def test_rtc_image(root: pathlib.Path) -> None:
    case = root / "rtc-image"
    (case / "state").mkdir(parents=True)
    qmx = case / "machine.qmx"

    write_qmx(
        qmx,
        'serial = none\nnvram = file="state/machine.cmos",format=cmos128,rtc_init=time0\n',
    )
    launch(["-qmx", str(qmx)], root)
    cmos = case / "state" / "machine.cmos"
    if not cmos.exists() or cmos.stat().st_size != 128:
        raise AssertionError("rtc-image: time0 did not create a 128-byte CMOS image")

    write_qmx(
        qmx,
        'serial = none\nnvram = file="state/machine.cmos",format=cmos128,rtc_init=image\n',
    )
    _, stderr = launch(["-qmx", str(qmx)], root)
    if "invalid RTC date/time" in stderr:
        raise AssertionError(f"rtc-image: generated image was rejected\n{stderr}")


def test_bare_and_explicit(root: pathlib.Path) -> None:
    case = root / "invocation-forms"
    case.mkdir()
    qmx = case / "machine.qmx"
    write_qmx(qmx, "serial = none\n")

    launch([str(qmx)], root)
    launch(["-qmx", str(qmx)], root)


def main() -> int:
    if not QEMU.exists():
        print(f"QEMU binary not found: {QEMU}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="qmx-integration-") as tmp:
        root = pathlib.Path(tmp)
        test_missing_media(root)
        test_relative_paths(root)
        test_cli_precedence(root)
        test_rtc_image(root)
        test_bare_and_explicit(root)

    print("QMX integration tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
