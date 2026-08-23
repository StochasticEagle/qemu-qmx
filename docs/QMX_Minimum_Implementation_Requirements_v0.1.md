# QMX Minimum Implementation Requirements v0.1

This document supplements `QMX_Format_Specification_v0.1.md` and defines the minimum acceptance requirements for the first usable QMX implementation.

## Required launch-equivalent configuration

QMX v1 must be able to represent and launch a VM equivalent to the following existing QEMU configuration without a wrapper script:

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

-fw_cfg name=opt/org.seabios/setup,string=1
```

The initial QMX implementation therefore must support equivalents for:

- SDL display output;
- 384 MB guest RAM;
- KVM acceleration;
- the QEMU `pentium3` CPU model, including the portable Bochs `p3_katmai` mapping;
- `pc` machine type with ACPI disabled;
- Cirrus VGA;
- SDL audio backend;
- Sound Blaster 16 attached to the selected audio backend;
- boot menu enabled with explicit boot-class order;
- QCOW2 hard disks;
- raw read-only CD-ROM images;
- explicit primary/secondary IDE bus and master/slave placement;
- fw_cfg string items, including `opt/org.seabios/setup`.

## Example QMX representation

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

#qemu: fw_cfg.name="opt/org.seabios/setup", string="1"
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

## Failure policy

QMX distinguishes configuration errors from media-availability errors.

### Fatal configuration errors

The following remain fatal:

- the QMX file itself cannot be opened;
- malformed QMX syntax;
- unknown or unsupported required directive;
- invalid machine type;
- invalid CPU model;
- impossible device topology, including duplicate use of the same IDE slot;
- invalid numeric or enum values;
- a required BIOS/ROM image cannot be loaded.

Diagnostics should identify the QMX file and source directive/line when possible.

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

The initial implementation must support:

```text
#qemu: fw_cfg.name="opt/org.seabios/setup", string="1"
```

which maps to:

```text
-fw_cfg name=opt/org.seabios/setup,string=1
```

fw_cfg is only the Setup-entry/configuration input mechanism. Persistent BIOS settings remain in the file-backed CMOS/NVRAM state.
