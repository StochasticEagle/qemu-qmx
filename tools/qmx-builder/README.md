# QEMU QMX Builder

QEMU QMX Builder is a native Qt 6 desktop application for creating, editing,
validating, and launching QEMU QMX virtual-machine configurations.

## Design

- QMX remains ordinary human-readable text.
- Comments, blank lines, unknown options, and their ordering are preserved.
- The selected QEMU executable supplies its option, machine, CPU, and device
  capabilities dynamically through QMP.
- Validation uses QEMU's own `-qmx-check` implementation.
- QEMU and all discovery processes are invoked directly with argument vectors;
  no shell command is constructed.
- QEMU QMX Builder is a QEMU component built from `tools/qmx-builder`.

## Current 0.1 scope

- New, open, save, and save-as QMX documents.
- Dedicated form for core machine settings.
- Structured CPU model, CPU property, and SMP topology fields that serialize
  back to QEMU's standard comma-separated `cpu` and `smp` option values.
- Generic assignment editor covering every QMX parameter.
- Live, editable QMX source view.
- Automatic dynamic discovery of command-line options, machines, CPUs, and
  accelerators from the selected QEMU executable.
- Architecture selection across QEMU's system-emulation targets, with
  installed targets enabled and missing `qemu-system-*` executables identified.
- Architecture is builder state only and is never serialized into QMX. The
  selected architecture determines the QEMU executable; `machine` remains the
  corresponding QMX option.
- Validation with the selected QEMU executable.
- Validated launch of saved QMX files through QEMU's explicit `-qmx` option.
  QEMU is started as a detached process and does not depend on QEMU QMX Builder
  remaining open.
- Disk-image creation through the selected QEMU installation's `qemu-img`,
  with optional insertion of the new drive into the open QMX document.
- Round-trip parser tests.

Firmware pairing, device-property forms, and richer topology editing are
planned additions on top of this working foundation.

## Build

QEMU QMX Builder uses QEMU's Meson build. It requires a C++ compiler and Qt 6.5 or
newer with the Core and Widgets components. Qt Test enables its unit test.

```text
mkdir build
cd build
../configure --enable-qmx-builder
ninja
ninja test
```

It is auto-detected by default. Use `--disable-qmx-builder` to omit it, or
`--enable-qmx-builder` to require it and report a configuration error when a
dependency is unavailable. Install QEMU and QEMU QMX Builder together with:

```text
ninja install
```

## License

GPL-2.0-or-later. Individual source files carry SPDX license identifiers.
