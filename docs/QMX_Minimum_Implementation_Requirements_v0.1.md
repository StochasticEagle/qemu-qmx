# QMX Minimum Implementation Requirements v0.1

This document supplements `QMX_Format_Specification_v0.1.md` and defines the minimum acceptance requirements for the first usable QMX implementation.

## Required parameter families

The concrete Windows 98 configuration below is an acceptance fixture, not a set of QMX defaults:

```text
qemu-system-x86_64
-display sdl
-m 384
-enable-kvm
-cpu pentium3
-machine pc,acpi=off
-vga cirrus

-audiodev sdl,id=snd0
-device sb16,audiodev=snd0

-boot menu=on,order=ca

-drive file=windows98se.qcow2,format=qcow2,if=none,id=win98hdd
-device ide-hd,drive=win98hdd,bus=ide.0,unit=0

-drive file=Windows98_SE.iso,format=raw,media=cdrom,if=none,id=win98cd,readonly=on
-device ide-cd,drive=win98cd,bus=ide.1,unit=0

-fw_cfg name=opt/seabios/setup,string=1
```

QMX MUST be able to represent that configuration, but none of those values become QMX defaults.

The initial implementation must support the following **parameter families** generically:

- display backend and its valid QEMU properties;
- guest memory size;
- accelerator selection and accelerator properties;
- CPU model and CPU properties;
- machine type and machine properties;
- VGA/display-adapter selection;
- audio backends and their properties;
- audio devices and their properties;
- boot menu and boot-order settings;
- block backends, including QEMU-supported image formats and drive properties;
- device attachment, including explicit IDE bus/unit placement;
- `fw_cfg` items and their supported QEMU properties.

The values shown in the Windows 98 fixture (`sdl`, `384`, `kvm`, `pentium3`, `pc,acpi=off`, `cirrus`, `sb16`, `qcow2`, `raw`, and so on) are examples that must work, not a whitelist.

### QEMU-profile value coverage

For a QMX file using `#qmx: profile=qemu`, QMX SHOULD support every value that the running QEMU build supports for the mapped parameter family. QMX must not maintain a separate hard-coded whitelist of QEMU display backends, accelerators, CPU models, machine types, VGA devices, audio backends/devices, block formats, device properties, or `fw_cfg` values unless translation is technically required.

Where possible, QMX should translate its structured input directly into QEMU's existing option/configuration objects and let the existing QEMU parser and device model validate the value. This keeps QMX capability synchronized with the QEMU build instead of freezing QMX to the examples in this document.

Examples:

- `#qemu: display=sdl` is valid when that QEMU build provides SDL; another valid display backend supported by the build should also be accepted through the same parameter family.
- `#qemu: accel=kvm` is one valid accelerator; other accelerators supported by the running QEMU build should be expressible without changing the QMX grammar.
- `cpu:` / QEMU CPU configuration must not be limited to `pentium3`; the QEMU profile must be able to select other CPU models and supported CPU properties.
- `#qemu: machine=pc` is one machine type; other machine types supported by the running QEMU binary should be selectable.
- `vga: extension=cirrus` is one portable/legacy mapping; QEMU-profile configuration must also be able to select other VGA/display devices supported by QEMU.
- `sb16` is an acceptance-test audio device, not the only permitted audio device.
- `qcow2` and `raw` are acceptance-test block formats, not the only permitted formats.
- the example boot order `ca` / `cdrom, disk` is not a default; other valid boot-order combinations must be supported.
- the example `fw_cfg` item is not special-cased as the only usable item; general supported `fw_cfg` entries must be representable.

The Portable QMX profile may define a smaller Bochs-compatible vocabulary where semantic translation is necessary. That restriction must not be imposed on the QEMU profile.

## Defaults and omitted parameters

QMX does not redefine QEMU defaults merely by adding support for a parameter family.

If a QMX file omits a parameter, the effective value should be the same value QEMU would normally use in the corresponding situation unless another QMX directive necessarily implies an explicit device or property.

Examples:

- omitting a display directive does not imply SDL;
- omitting memory does not imply 384 MB;
- omitting accelerator configuration does not imply KVM;
- omitting CPU configuration does not imply Pentium III;
- omitting machine configuration does not imply `pc,acpi=off` beyond QEMU's own normal default selection;
- omitting VGA configuration does not imply Cirrus;
- omitting audio does not imply SB16 or SDL audio;
- omitting a boot directive does not imply `ca`;
- omitting a drive does not create the Windows 98 example drive;
- omitting the SeaBIOS Setup `fw_cfg` item does not request Setup entry.

## Example QMX representation

The following is one test fixture corresponding to the Windows 98 configuration above:

```text
#qmx: version=1
#qmx: profile=qemu
#qmx: name="Windows 98 SE"

#qemu: display=sdl
#qemu: accel=kvm
#qemu: machine=pc
#qemu: acpi=off

memory: guest=384, host=384
cpu: model=p3_katmai, count=1
vga: extension=cirrus

#qemu: audiodev.snd0.driver=sdl
#qemu: device.sb16.audiodev=snd0

boot: cdrom, disk

ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15

ata0-master: type=disk, path="windows98se.qcow2"
#qemu: disk-format.ata0-master=qcow2

ata1-master: type=cdrom, path="Windows98_SE.iso", status=inserted
#qemu: disk-format.ata1-master=raw
#qemu: readonly.ata1-master=on

#qemu: fw_cfg.name="opt/seabios/setup", string="1"
```

Every relative path must resolve relative to the directory containing the `.qmx` file, not QEMU's current working directory.

## QMX must be additive

If no QMX file is specified, QEMU must follow the normal upstream QEMU argument-processing and machine-creation path. QMX must not change defaults, path handling, media failure behavior, firmware behavior, device creation, or command-line semantics for ordinary QEMU invocations.

Both forms are required:

```text
qemu-system-x86_64 machine.qmx
qemu-system-x86_64 -qmx machine.qmx
```

## Device topology

QMX must preserve explicitly declared hardware placement. For the initial legacy-PC target:

- `ata0-master` maps to `ide.0`, unit 0;
- `ata0-slave` maps to `ide.0`, unit 1;
- `ata1-master` maps to `ide.1`, unit 0;
- `ata1-slave` maps to `ide.1`, unit 1.

A device must not be silently attached to another available slot if its configured slot cannot be created.

QEMU-profile generic device configuration may additionally express other QEMU buses, devices, and properties as support is implemented; the four legacy IDE names above are required portable convenience mappings, not the complete QEMU device model.

## Failure policy

QMX distinguishes configuration errors from media-availability errors.

### Fatal configuration errors

The following remain fatal:

- the QMX file itself cannot be opened;
- malformed QMX syntax;
- unknown or unsupported required directive;
- a parameter value rejected by the underlying QEMU subsystem (for example, an unavailable machine type or CPU model);
- impossible device topology, including duplicate use of the same explicitly assigned IDE slot;
- invalid numeric or enum values;
- a required BIOS/ROM image cannot be loaded.

Diagnostics should identify the QMX file and source directive/line when possible and should preserve the useful diagnostic from the underlying QEMU parser where applicable.

### Non-fatal disk and removable-media failures

A drive, CD-ROM, or floppy image declared by QMX must not prevent the rest of the VM from starting merely because its backing file is unavailable.

At minimum, these conditions are non-fatal:

- file not found;
- permission/access denied;
- removable host device absent;
- path exists but cannot currently be opened;
- image cannot be opened using its configured mode.

QEMU must issue a clear warning containing the configured device, resolved host path, OS/error reason, and action taken, then continue with that device slot empty.

Example:

```text
QMX warning: ata1-master: cannot open '/vm/Windows98_SE.iso': No such file or directory; continuing with Secondary IDE Master empty
```

Example:

```text
QMX warning: ata0-master: cannot open '/vm/windows98se.qcow2': Permission denied; continuing with Primary IDE Master empty
```

This tolerant startup policy applies only to media declared through QMX. Traditional explicit QEMU `-drive` command-line behavior remains unchanged.

### Invalid or corrupt image files

QMX v1 defaults to tolerant startup for media images:

- missing or inaccessible image: warn, omit device, continue;
- format cannot be determined when no explicit format is present: warn, omit device, continue;
- explicitly declared format but invalid/corrupt image: warn, omit device, continue;
- initial host I/O failure: warn, omit device, continue.

A future strict-media mode may promote these errors to fatal startup failures.

## Persistent CMOS failure

A configured `cmosimage` is persistent firmware state rather than guest storage. If it cannot be opened, QEMU should warn and continue with volatile CMOS using normal machine defaults, unless the QMX explicitly marks persistent CMOS as required.

Example:

```text
QMX warning: cannot open '/vm/machine.cmos' for persistence: Permission denied; continuing with volatile CMOS
```

A missing writable CMOS image should normally be created automatically from normal machine CMOS defaults.

## SeaBIOS Setup entry

The initial implementation must support this particular acceptance-test item:

```text
#qemu: fw_cfg.name="opt/seabios/setup", string="1"
```

which maps to:

```text
-fw_cfg name=opt/seabios/setup,string=1
```

It is an example of general `fw_cfg` support, not a QMX default or a special-purpose restriction. `fw_cfg` remains only an input/configuration mechanism; persistent BIOS settings remain in the file-backed CMOS/NVRAM state.
