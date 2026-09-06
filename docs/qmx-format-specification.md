# QMX Virtual Machine Configuration Format

## Version 1

QMX is QEMU's human-readable virtual-machine configuration format. A QMX
file describes the same parameters accepted by the QEMU system emulator,
without shell syntax and without the leading `-` used on the command line.

QMX does not define a second catalog of QEMU options. Except for the explicit
differences in this document, parameter names, values, devices, properties,
and availability are defined by the running QEMU build. See the
[QEMU invocation documentation](system/invocation.rst) and the documentation
for the relevant device or subsystem.

## Invocation

QEMU accepts either form:

```text
qemu-system-x86_64 machine.qmx
qemu-system-x86_64 -qmx machine.qmx
```

This form validates QMX syntax and translation without creating a machine:

```text
qemu-system-x86_64 -qmx-check machine.qmx
```

Explicit command-line parameters take precedence over corresponding QMX
parameters for that invocation. They do not modify the file. For repeatable
objects, a command-line object with the same `id` replaces only that QMX
object. `fw_cfg` entries are matched by `name`.

## Syntax

A document contains blank lines, comments, and assignments:

```text
qmx = 1
# A comment occupies its own line.
memory = 4G
machine = q35,accel=kvm
drive.system = file=./disks/system.qcow2,format=qcow2,if=none
device.system = virtio-blk-pci,drive=system
```

The line grammar is:

```text
document    := line*
line        := blank | comment | assignment
comment     := space* "#" text
assignment  := key space* "=" space* value
key         := identifier ("." identifier)?
identifier  := [A-Za-z_][A-Za-z0-9_-]*
```

The required `qmx = 1` assignment declares the file-format major version.
Assignments may appear in any order. Each complete key may occur only once.

The part before an optional dot is a QEMU parameter name. The suffix is an
occurrence name that makes repeated parameters distinct:

```text
numa.node0 = node,nodeid=0
numa.cpu0 = cpu,node-id=0,socket-id=0,core-id=0,thread-id=0
```

For QEMU object parameters that use `id=`, such as `drive`, `device`,
`chardev`, `netdev`, `audiodev`, `fsdev`, `object`, and `tpmdev`, QMX derives
the QEMU ID from the occurrence name. An explicit `id=` is allowed only when
it is identical to that name.

Values use the syntax of the corresponding QEMU parameter. A comma-separated
QEMU property list remains a comma-separated property list. Whitespace around
the QMX assignment `=` is ignored; whitespace inside the value follows that
parameter's QEMU syntax. Double quotes preserve spaces and commas in a value.
The supported escapes are `\\`, `\"`, `\n`, and `\t`.

QMX performs no shell expansion: no word splitting, globbing, environment
substitution, command substitution, pipelines, or redirection occurs. Each
assignment is translated as one parameter value for QEMU's existing option
parser.

## Differences from the QEMU command line

QMX intentionally differs from command-line spelling only where listed here:

- Parameter names omit their leading `-`.
- Memory is written as `memory`, not the command-line abbreviation `m`.
- An argumentless QEMU switch uses `on` or `off`. `on` enables the switch;
  `off` omits it.
- A suffix such as `.disk0` distinguishes occurrences of a repeatable
  parameter and supplies `id=disk0` for QEMU object parameters that use IDs.
- `description` is QMX metadata and has no runtime effect.
- `nvram` is the QMX persistent legacy CMOS facility described below.
- QMX-defined ordinary removable and disk media use the tolerant startup
  policy described below.
- QMX applies the path rules in the next section.

The abbreviation `m` is not a QMX parameter. All other QEMU parameter names
retain their QEMU spelling. Whether a parameter or value is valid remains a
property of the selected QEMU executable, target, machine, and compiled
features.

## Paths and firmware lookup

VM-owned paths are resolved relative to the directory containing the QMX
file. This includes known file properties such as `drive.file`, `fw_cfg.file`,
`chardev.path`, and `object.mem-path`, plus `nvram.file`. An absolute path is
used directly. A `fat:` or `fat:rw:` drive keeps the prefix and resolves the
directory that follows it relative to the QMX file.

Read-only pflash firmware follows QEMU's system-firmware lookup rules:

- `/absolute/name.fd` uses that absolute path;
- `./name.fd` explicitly selects a file beside the QMX file;
- bare `name.fd` is searched in QEMU's configured firmware and data
  directories, including paths configured by the distributor and `-L`.

For example:

```text
drive.code = if=pflash,format=raw,readonly=on,file=edk2-x86_64-code.fd
drive.vars = if=pflash,format=raw,file=./state/vars.fd
```

The first file is system firmware found by QEMU. The second is VM-local,
writable state. Firmware filenames vary by QEMU build and operating-system
package; use a filename supplied by that installation.

## QMX metadata and persistent CMOS

`description` stores human-readable metadata:

```text
description = "Development machine"
```

It does not become a QEMU command-line parameter.

QMX also defines an optional persistent 128-byte legacy CMOS image:

```text
nvram = file=./state/machine.cmos,format=cmos128,rtc_init=time0
```

`rtc_init=time0` loads configuration bytes while initializing the clock from
QEMU's configured time. `rtc_init=image` also loads clock bytes from the
image. This facility is available only on compatible machine types. Failure
to open or persist it produces a warning and leaves volatile CMOS available.

## Media failure policy

If ordinary media named by a QMX `drive` is missing or unreadable, QEMU warns,
omits that drive, and omits QMX devices that depend on it. The configured slot
is not reassigned. Read-only system pflash firmware is required and therefore
fails validation when it cannot be found or read.

Media supplied explicitly on the command line retains normal QEMU failure
behavior. Malformed QMX and invalid QEMU configuration remain fatal.

## Complete example

```text
qmx = 1
name = "Development VM"
description = "Local development environment"

machine = q35
memory = 4G
accel = kvm
cpu = host
display = gtk

drive.system = file=./disks/system.qcow2,format=qcow2,if=none
device.system = virtio-blk-pci,drive=system

chardev.swtpm = socket,path=./run/swtpm.sock
tpmdev.tpm0 = emulator,chardev=swtpm
device.tpm = tpm-tis,tpmdev=tpm0

drive.code = if=pflash,format=raw,readonly=on,file=edk2-x86_64-code.fd
drive.vars = if=pflash,format=raw,file=./state/vars.fd

boot = menu=on
nodefaults = on
```
