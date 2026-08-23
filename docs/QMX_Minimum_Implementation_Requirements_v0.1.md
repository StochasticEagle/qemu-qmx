# QMX Minimum Implementation Requirements v0.1

This document supplements `QMX_Format_Specification_v0.2.md` and defines the minimum acceptance requirements for the first usable QMX implementation.

## 1. QMX is additive

If no QMX file is specified, QEMU must follow its normal upstream argument-processing and machine-creation path. QMX must not change ordinary QEMU defaults, path handling, media failure behavior, firmware behavior, device creation, or command-line semantics.

Both forms are required:

```text
qemu-system-x86_64 machine.qmx
qemu-system-x86_64 -qmx machine.qmx
```

A parse/translation-only validation mode is also required:

```text
qemu-system-x86_64 -qmx-check machine.qmx
```

`-qmx-check` must validate QMX syntax, QMX family/property semantics, and QMX-to-QEMU argument translation without creating or starting a virtual machine.

## 2. Required grammar

The initial implementation must parse the compact QEMU-native grammar:

```text
key = value
family.id = value
family.id = property=value,property=value,...
```

Comments begin with `#`. Quoted strings use double quotes. Parsing must never invoke a shell.

Duplicate scalar keys and duplicate object keys are fatal errors.

## 3. Required parameter families

The implementation must support these scalar families:

- `name`
- `description`
- `machine`
- `memory`
- `accel`
- `cpu`
- `display`
- `vga`
- `bios`
- `boot`
- `serial`
- `parallel`
- `monitor`
- `smp`
- `rtc`
- `usb`
- `nvram`

It must support these named object families:

- `audiodev.<id>`
- `device.<id>`
- `drive.<id>`
- `fw_cfg.<id>`
- `netdev.<id>`
- `chardev.<id>`
- `object.<id>`

`description` is frontend metadata only. It must be accepted by QEMU but must not alter machine runtime semantics.

`usb` accepts `on` or `off`; `on` maps to QEMU's legacy `-usb` switch and `off` emits no switch.

For mapped QEMU families, values should be handed to QEMU's existing configuration parsers and device models wherever practical. The QMX implementation must not impose a separate whitelist when QEMU can validate the value directly.

The Windows 98 fixture values are examples that must work, not defaults or a whitelist.

## 4. Defaults and omitted parameters

Omitting a QMX setting leaves the corresponding behavior to normal QEMU defaults unless another explicitly configured object necessarily implies a value.

Therefore, QMX does not imply SDL, 384 MB RAM, KVM, Pentium III, `pc`, ACPI disabled, Cirrus VGA, SB16, a particular boot order, any drive, or SeaBIOS Setup entry unless those settings are present in the QMX file.

## 5. Required Windows 98 acceptance fixture

The initial implementation must be able to represent and launch the semantic equivalent of:

```text
qemu-system-x86_64 \
  -display sdl \
  -m 384 \
  -enable-kvm \
  -cpu pentium3 \
  -machine pc,acpi=off \
  -vga cirrus \
  -audiodev sdl,id=snd0 \
  -device sb16,audiodev=snd0 \
  -boot menu=on,order=ca \
  -drive file=windows98se.qcow2,format=qcow2,if=none,id=win98hdd \
  -device ide-hd,drive=win98hdd,bus=ide.0,unit=0 \
  -drive file=Windows98_SE.iso,format=raw,media=cdrom,if=none,id=win98cd,readonly=on \
  -device ide-cd,drive=win98cd,bus=ide.1,unit=0 \
  -fw_cfg name=opt/seabios/setup,string=1
```

using:

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

drive.win98hdd = file="windows98se.qcow2",format=qcow2,if=none
device.hdd = ide-hd,drive=win98hdd,bus=ide.0,unit=0

drive.win98cd = file="Windows98_SE.iso",format=raw,media=cdrom,if=none,readonly=on
device.cdrom = ide-cd,drive=win98cd,bus=ide.1,unit=0

fw_cfg.setup = name="opt/seabios/setup",string="1"

nvram = file="machine.cmos",format=cmos128,rtc_init=time0
```

The `nvram` line is additional persistent firmware state and is not part of the legacy command-line fixture.

## 6. Object IDs

The suffix in a named QMX object is a stable QMX-local identifier:

```text
drive.win98hdd = ...
device.hdd = ...
audiodev.snd0 = ...
netdev.net0 = user
chardev.console = null
object.mem0 = memory-backend-ram,size=128M
```

Where the corresponding QEMU option requires an `id`, QMX should normally derive it from the object suffix rather than requiring duplicated `id=` syntax.

References such as `drive=win98hdd` must resolve to the corresponding named object.

## 7. Relative paths

Every relative filesystem path originating from a QMX path-valued setting must resolve relative to the directory containing the QMX file, never relative to QEMU's current working directory.

Absolute paths remain absolute.

Only semantically path-valued properties are rewritten. Arbitrary string values are not treated as paths.

## 8. Media failure policy

Media availability failures for QMX-defined drives are non-fatal by default.

At minimum, the following must warn and continue:

- file not found;
- access denied;
- removable host device absent;
- backing file cannot currently be opened;
- configured image format cannot open or validate the image;
- initial host I/O failure while opening media.

The warning must identify the QMX object, resolved path, error reason, and action taken.

If a failed drive is referenced by a device object, that device is omitted and its explicitly configured slot remains empty. It must not be moved to another bus or unit.

Traditional explicit QEMU command-line media failure behavior remains unchanged.

Configuration errors remain fatal, including malformed syntax, duplicate keys, unsupported QMX major version, impossible topology, and non-media values rejected by the relevant QEMU subsystem.

## 9. Persistent CMOS

The minimum implementation must support:

```text
nvram = file="machine.cmos",format=cmos128,rtc_init=time0
```

`cmos128` is exactly 128 bytes of legacy PC CMOS state. This binary layout may be compatible with other emulators, but QMX itself is QEMU-specific.

If the writable file does not exist, QEMU should create it from initialized machine CMOS defaults. If it cannot be opened, QEMU should warn and continue with volatile CMOS.

Guest writes must update normal emulated CMOS, and persistent changes should be flushed promptly and on normal QEMU shutdown.

## 10. CLI precedence

Traditional QEMU command-line options override QMX values.

Scalar QMX settings are suppressed when their corresponding CLI option is explicitly present. For named object families, a CLI object suppresses only the QMX object with the same ID; unrelated QMX objects remain active. `fw_cfg` conflicts are resolved by `name=`.

This applies to the required scalar and object families, including `netdev`, `chardev`, and `object`.

## 11. Conversion requirement

The parser should produce a simple representation equivalent to tuples of:

```text
(family, optional-id, value)
```

This representation must not depend on shell command text and should be straightforward to consume by conversion tooling targeting QEMU CLI, libvirt XML, Proxmox configuration, or other formats.
