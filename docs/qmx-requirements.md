# QMX Requirements and Release Criteria

This document defines the minimum implementation, compatibility, testing, and release requirements for QMX. It supplements `qmx-format-specification.md`.

Current version: 0.9

## 1. Scope and Invocation

QMX is additive. Without a QMX file, QEMU must retain normal upstream argument processing, defaults, path handling, firmware behavior, device creation, and media-failure semantics.

Both launch forms are required:

```text
qemu-system-x86_64 machine.qmx
qemu-system-x86_64 -qmx machine.qmx
```

Parse-and-translation validation is required without machine creation:

```text
qemu-system-x86_64 -qmx-check machine.qmx
```

## 2. Format and Parameter Coverage

The parser must accept:

```text
key = value
family.id = value
family.id = property=value,property=value,...
```

Comments begin with `#`; quoted strings use double quotes. Parsing must never invoke a shell. Duplicate scalar or object keys are fatal errors.

Required scalar families:

- `name`, `description`, `machine`, `memory`, `accel`, `cpu`
- `display`, `vga`, `bios`, `boot`
- `serial`, `parallel`, `monitor`, `smp`, `rtc`, `usb`
- `nvram`

Required named object families:

- `audiodev.<id>`, `device.<id>`, `drive.<id>`, `fw_cfg.<id>`
- `netdev.<id>`, `chardev.<id>`, `object.<id>`

`description` is metadata and must not change runtime behavior. `usb` accepts `on` or `off`; `on` maps to legacy `-usb`, while `off` emits no switch.

Mapped values should use QEMU's existing parsers and device models rather than a separate QMX whitelist. Omitted settings retain normal QEMU defaults.

The suffix of a named object is its stable ID. QMX should derive QEMU `id=` values from it, and references such as `drive=system` must resolve to the matching object.

The internal representation should remain equivalent to:

```text
(family, optional-id, value)
```

It must not depend on shell command text and should remain suitable for future conversion tools.

## 3. Required Behavior

### 3.1 Relative Paths

Relative paths resolve from the QMX file's directory; absolute paths remain absolute. Only properties with defined path semantics are rebased:

- `bios`
- `nvram.file`
- `drive.<id>.file`
- `fw_cfg.<id>.file`
- `chardev.<id>.path` and `chardev.<id>.logfile`
- `netdev.<id>.script`, `downscript`, and `vhostdev`
- `object.<id>.file`, `filename`, and `mem-path`
- `serial`, `parallel`, and `monitor` values using `file:`, `pipe:`, or `unix:`

Arbitrary strings must not be rewritten merely because they resemble paths.

### 3.2 Media Failures

QMX-defined media failures are non-fatal by default, including missing files, access denial, absent removable devices, open or validation failures, and initial host I/O errors.

Warnings must identify the QMX object, resolved path, error, and action taken. If a failed drive is referenced by `device.*`, that device is omitted and its configured slot remains empty. It must not be moved elsewhere.

Explicit command-line media retains normal QEMU failure behavior. Configuration errors remain fatal.

### 3.3 Command-Line Precedence

Explicit QEMU command-line options override corresponding QMX values. A same-ID command-line object suppresses only its QMX counterpart; unrelated objects remain. `fw_cfg` conflicts are resolved by `name=`.

### 3.4 Persistent CMOS

The minimum implementation must support:

```text
nvram = file="machine.cmos",format=cmos128,rtc_init=time0
```

`cmos128` is exactly 128 bytes. If absent, the file is created from initialized CMOS defaults. If it cannot be opened, QEMU warns and uses volatile CMOS. Guest changes should be flushed promptly and on normal shutdown.

## 4. Required Acceptance Fixture

The following must parse and create the intended Microsoft Windows 98 virtual machine. These values are an acceptance fixture, not defaults or a whitelist.

```text
qmx = 1
name = "Windows 98 SE"
description = "Windows 98 SE virtual machine"

machine = pc,acpi=off
memory = 384M
accel = kvm
cpu = pentium3
display = sdl
vga = cirrus

audiodev.snd0 = sdl
device.sound = sb16,audiodev=snd0
boot = menu=on,order=ca

drive.hd0 = file="hd0.qcow2",format=qcow2,if=none
device.hdd = ide-hd,drive=hd0,bus=ide.0,unit=0
drive.cd0 = file="Windows98_SE.iso",format=raw,media=cdrom,if=none,readonly=on
device.cdrom = ide-cd,drive=cd0,bus=ide.1,unit=0

fw_cfg.setup = name="opt/seabios/setup",string="1"
nvram = file="machine.cmos",format=cmos128,rtc_init=time0
```

## 5. Automated Release Verification

The beta candidate must pass from a clean build:

```text
python tests/qmx/test-qmx-parser.py build/qemu-system-x86_64
python tests/qmx/test-qmx-integration.py build/qemu-system-x86_64
```

Automated coverage must verify:

1. both QMX invocation forms and `-qmx-check`;
2. grammar, duplicate-key rejection, family/property validation, and QMX-to-QEMU translation;
3. non-fatal missing floppy, hard-disk, CD-ROM, and removable-host media handling;
4. omission of devices that reference unavailable drives;
5. every relative-path adapter listed in Section 3.1;
6. scalar and same-ID object command-line precedence, including `fw_cfg` conflicts;
7. persistent CMOS creation with `rtc_init=time0`, reload with `rtc_init=image`, exact 128-byte size, and valid RTC state; and
8. successful machine creation without requiring a guest operating system.

## 6. QEMU and SeaBIOS Compatibility Matrix

The setup-menu integration is optional across independently versioned QEMU and SeaBIOS projects. Either project must remain usable when upgraded first.

- **Upstream QEMU:** no QMX setup-menu integration.
- **QMX QEMU:** QMX plus setup-menu integration.
- **Upstream SeaBIOS:** no interactive setup menu.
- **Setup-menu SeaBIOS:** interactive setup-menu capability.

Every run must record exact QEMU and SeaBIOS commit hashes.

| QEMU | SeaBIOS | Required result |
| --- | --- | --- |
| Upstream | Upstream | Normal firmware initialization and boot; no QMX setup menu expected. |
| Upstream | Setup-menu | Normal boot without requiring QMX-aware QEMU; the menu may be unavailable. |
| QMX | Upstream | Normal boot even when the setup `fw_cfg` item is present; older SeaBIOS may ignore it. |
| QMX | Setup-menu | Normal boot; with `fw_cfg.setup = name="opt/seabios/setup",string="1"`, the menu must be available and usable. Omission must preserve normal behavior. |

### 6.1 Automated Compatibility Checks

Continuous integration should automate all deterministic portions of the matrix. Each combination must:

1. use pinned QEMU and SeaBIOS commits;
2. start a minimal deterministic boot fixture;
3. fail on aborts, firmware failures, timeouts, or failure to reach a machine-readable boot marker;
4. log both commits, machine type, accelerator, and fixture metadata; and
5. run without network access after sources and build dependencies are acquired.

QMX tests must also verify that:

- the QMX setup object creates `opt/seabios/setup` with the expected value;
- omission does not create it implicitly;
- equivalent QMX and command-line configurations produce the same value; and
- upstream SeaBIOS still boots when the item is present.

### 6.2 Interactive Validation

With QMX QEMU and setup-menu SeaBIOS, testing must confirm that the setup entry appears, accepts input, changes at least one supported setting, and resumes normal boot without corrupting firmware state.

This should use deterministic keyboard injection and a machine-readable firmware marker. Until such a marker exists, the interactive portion may remain a documented manual release test. Screenshot comparison or OCR alone is not a sufficient long-term behavioral test.

A missing menu is a failure only for QMX QEMU plus setup-menu SeaBIOS when the enabling `fw_cfg` item is present. Failure to initialize firmware or reach the boot fixture is always a compatibility failure.

## 7. Deferred Features

The following are not blockers and are not guaranteed to be implemented:

- include/import support;
- variables or constants;
- canonical QEMU argument or external-format conversion output; and
- a separate `qmx-info` or dump utility.

## 8. Release Decision

QMX is feature-frozen when the clean-build regression suites and all deterministic compatibility-matrix tests pass. Any remaining manual interactive test must be completed and recorded before release.

After beta, work before release should prioritize compatibility, diagnostics, and regression fixes rather than new language features.
