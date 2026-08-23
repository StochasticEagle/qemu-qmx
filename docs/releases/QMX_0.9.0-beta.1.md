# QMX 0.9.0-beta.1

QMX 0.9.0-beta.1 is the first beta release of the QMX machine-configuration support for QEMU.

## Release status

The repository parser and launch-level integration suites pass against the release candidate build:

```text
python tests/qmx/test-qmx-parser.py build/qemu-system-x86_64
QMX parser tests passed

python tests/qmx/test-qmx-integration.py build/qemu-system-x86_64
QMX integration tests passed
```

## Included QMX functionality

- Positional `machine.qmx` invocation and explicit `-qmx machine.qmx` invocation.
- `-qmx-check machine.qmx` parse/translation-only validation.
- Scalar families: `name`, `description`, `machine`, `memory`, `accel`, `cpu`, `display`, `vga`, `bios`, `boot`, `serial`, `parallel`, `monitor`, `smp`, `rtc`, `usb`, and `nvram`.
- Named object families: `audiodev.<id>`, `device.<id>`, `drive.<id>`, `fw_cfg.<id>`, `netdev.<id>`, `chardev.<id>`, and `object.<id>`.
- CLI-over-QMX precedence, including same-ID object replacement.
- QMX-relative path rebasing for defined path-valued settings.
- Non-fatal QMX media handling for unavailable drives, including omission of dependent `device.*` objects.
- Persistent 128-byte CMOS backing with both `rtc_init=time0` and `rtc_init=image`.
- SeaBIOS setup integration used by the QMX development branch.

## Explicitly deferred

The following are intentionally not part of the 0.9 beta scope:

- include/import support;
- variables/constants;
- canonical argv conversion/introspection output;
- a separate `qmx-info` or dump utility.

## Packaging policy

The Linux packages under `packaging/` are replacement packages, not side-by-side variants. They install the normal QEMU x86 system-emulator executable names and conflict with/replace the distribution-provided `qemu-system-x86` package where the package format supports those relationships.

Installing QEMU-QMX therefore replaces the stock x86 QEMU system-emulator package. To return to the distribution build, remove the QMX package and reinstall the distribution's `qemu-system-x86` package.

QEMU's upstream `VERSION` remains unchanged. QMX has its own release version in `QMX_VERSION` so QEMU build/version semantics are not overwritten.
