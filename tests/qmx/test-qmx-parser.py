#!/usr/bin/env python3
"""Focused QMX parser/precedence regression tests.

Usage:
    python tests/qmx/test-qmx-parser.py [build/qemu-system-x86_64]

Most tests stop at QEMU's normal -version path, so they exercise QMX parsing and
argument translation without starting a virtual machine.  -qmx-check tests use
the dedicated parse/translation-only path.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
QEMU = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/qemu-system-x86_64"


def run_qmx(text: str, *extra: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="qmx-test-") as tmp:
        qmx = pathlib.Path(tmp) / "machine.qmx"
        qmx.write_text(text, encoding="utf-8")
        return subprocess.run(
            [str(QEMU), "-qmx", str(qmx), *extra, "-version"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


def run_qmx_check(text: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="qmx-check-test-") as tmp:
        qmx = pathlib.Path(tmp) / "machine.qmx"
        qmx.write_text(text, encoding="utf-8")
        return subprocess.run(
            [str(QEMU), "-qmx-check", str(qmx)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )


def expect_ok(name: str, text: str, *extra: str) -> None:
    result = run_qmx(text, *extra)
    if result.returncode != 0:
        raise AssertionError(
            f"{name}: expected success, got {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def expect_check_ok(name: str, text: str) -> None:
    result = run_qmx_check(text)
    if result.returncode != 0:
        raise AssertionError(
            f"{name}: expected success, got {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if "QMX configuration is valid" not in result.stdout:
        raise AssertionError(
            f"{name}: expected check-mode success message\nstdout:\n{result.stdout}"
        )


def expect_fail(name: str, text: str, needle: str) -> None:
    result = run_qmx(text)
    if result.returncode == 0:
        raise AssertionError(f"{name}: expected failure")
    if needle not in result.stderr:
        raise AssertionError(
            f"{name}: expected {needle!r} in stderr\n"
            f"stderr:\n{result.stderr}"
        )


def expect_check_fail(name: str, text: str, needle: str) -> None:
    result = run_qmx_check(text)
    if result.returncode == 0:
        raise AssertionError(f"{name}: expected failure")
    if needle not in result.stderr:
        raise AssertionError(
            f"{name}: expected {needle!r} in stderr\n"
            f"stderr:\n{result.stderr}"
        )


def main() -> int:
    if not QEMU.exists():
        print(f"QEMU binary not found: {QEMU}", file=sys.stderr)
        return 2

    expect_ok(
        "comments and quoted strings",
        '''\
# comment
qmx = 1
name = "QMX test"
description = "Metadata for a frontend"
machine = pc,acpi=off
''',
    )

    expect_ok(
        "quoted comma",
        '''\
qmx = 1
fw_cfg.test = name="opt/qmx/test",string="one,two"
''',
    )

    expect_ok(
        "scalar CLI override",
        '''\
qmx = 1
memory = 384M
cpu = pentium3
''',
        "-m", "512M",
    )

    expect_ok(
        "same-id drive CLI override",
        '''\
qmx = 1
drive.disk = file="does-not-need-to-open.img",format=raw,if=none
''',
        "-drive", "file=cli-does-not-need-to-open.img,format=raw,if=none,id=disk",
    )

    expect_check_ok(
        "extended families",
        '''\
qmx = 1
description = "Extended family validation"
netdev.net0 = user
chardev.console = null
object.mem0 = memory-backend-ram,size=1M
serial = none
parallel = none
monitor = none
smp = 2
rtc = base=utc
usb = off
''',
    )

    expect_check_ok(
        "usb enabled",
        '''\
qmx = 1
usb = on
''',
    )

    expect_fail(
        "duplicate assignment",
        '''\
qmx = 1
memory = 384M
memory = 512M
''',
        "duplicate QMX assignment 'memory'",
    )

    expect_fail(
        "duplicate property",
        '''\
qmx = 1
drive.disk = file="a.img",file="b.img",if=none
''',
        "duplicate property 'file'",
    )

    expect_fail(
        "object id mismatch",
        '''\
qmx = 1
drive.disk = file="a.img",if=none,id=other
''',
        "does not match QMX object id 'disk'",
    )

    expect_fail(
        "unterminated quote",
        '''\
qmx = 1
name = "unterminated
''',
        "unterminated quoted string",
    )

    expect_fail(
        "unsupported escape",
        '''\
qmx = 1
name = "bad\\qescape"
''',
        "unsupported escape sequence",
    )

    expect_fail(
        "unsupported directive",
        '''\
qmx = 1
not_a_qmx_family = value
''',
        "unsupported QMX directive",
    )

    expect_check_fail(
        "bad usb value",
        '''\
qmx = 1
usb = maybe
''',
        "usb must be 'on' or 'off'",
    )

    expect_fail(
        "bad nvram format",
        '''\
qmx = 1
nvram = file="machine.cmos",format=wrong,rtc_init=time0
''',
        "nvram format must be 'cmos128'",
    )

    expect_fail(
        "bad nvram rtc mode",
        '''\
qmx = 1
nvram = file="machine.cmos",format=cmos128,rtc_init=wrong
''',
        "nvram rtc_init must be 'time0' or 'image'",
    )

    print("QMX parser tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
