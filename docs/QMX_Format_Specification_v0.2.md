# QMX Virtual Machine Configuration Format
## Draft Specification v0.2

**Status:** Design draft for implementation in a QEMU fork  
**Extension:** `.qmx`  
**Primary goal:** A compact, human-readable, QEMU-native virtual-machine definition format that maps directly onto QEMU configuration objects and is straightforward to convert to QEMU command lines, libvirt XML, Proxmox configuration, and other VM-description formats.

---

## 1. Design principles

QMX is a declarative VM configuration file for QEMU. It is not a shell script and it is not a Bochs configuration format.

The format is intentionally small:

```text
key = value
family.id = value
family.id = property=value,property=value,...
```

The design goals are:

1. one compact text file describes a VM;
2. simple settings normally occupy one line;
3. repeated QEMU objects have stable names;
4. values and property lists stay close to QEMU's existing option syntax;
5. QEMU's existing parsers and device models validate QEMU values wherever practical;
6. relative paths are always relative to the QMX file;
7. omitted settings preserve normal QEMU defaults;
8. parsing never invokes a shell;
9. the parsed representation is simple enough for external conversion tools;
10. mutable firmware state is kept outside the QMX file.

QMX does not require or attempt direct configuration-file compatibility with another emulator.

---

## 2. Invocation

A QMX-aware QEMU must support:

```text
qemu-system-x86_64 machine.qmx
qemu-system-x86_64 -qmx machine.qmx
```

When no QMX file is specified, QEMU must follow its normal upstream argument-processing and machine-creation path. QMX must not alter ordinary QEMU defaults or command-line behavior.

Explicit command-line options supplied together with a QMX file override corresponding QMX settings for that invocation. They do not rewrite the QMX file.

---

## 3. Grammar

### 3.1 Line grammar

A QMX document consists of blank lines, comments, and assignments.

```text
document    := line*
line        := blank | comment | assignment
comment     := optional-space "#" text
assignment  := key optional-space "=" optional-space value
key         := identifier ("." identifier)*
identifier  := [A-Za-z_][A-Za-z0-9_-]*
```

A value is either a scalar or a QEMU-style property list:

```text
value       := scalar | property-list
property-list := item ("," item)*
item        := scalar | identifier "=" scalar
```

The parser must recognize commas only when they occur outside quoted strings.

### 3.2 Whitespace

Whitespace surrounding `=` and commas is insignificant unless it occurs inside a quoted string.

These are equivalent:

```text
machine=pc,acpi=off
machine = pc, acpi=off
```

### 3.3 Comments

A line whose first non-whitespace character is `#` is a comment.

```text
# Windows 98 test VM
memory = 384M
```

QMX v0.2 defines no executable comment pragmas.

### 3.4 Strings

Double quotes delimit strings when quoting is necessary:

```text
name = "Windows 98 SE"
drive.cd = file="Install Media/Windows 98 SE.iso",format=raw
```

The minimum escape set is:

```text
\\   backslash
\"   double quote
\n   newline
\t   tab
```

The parser must not perform shell evaluation, command substitution, glob expansion, or shell-style word splitting.

### 3.5 Duplicate keys

A scalar key may appear only once.

An object key such as `drive.system` or `device.cdrom` may appear only once.

Duplicate assignments are fatal configuration errors rather than order-dependent overrides.

---

## 4. Keys and object families

QMX uses two forms of key.

### 4.1 Scalar or singleton family

```text
memory = 384M
cpu = pentium3
machine = pc,acpi=off
boot = menu=on,order=ca
```

The value is mapped to the corresponding QEMU configuration family.

### 4.2 Named repeated object

```text
drive.system = file="system.qcow2",format=qcow2,if=none
device.system = ide-hd,drive=system,bus=ide.0,unit=0

audiodev.snd0 = sdl
device.sound = sb16,audiodev=snd0
```

The suffix after the dot is a QMX-local stable identifier. It is used for references, diagnostics, and conversion. When the corresponding QEMU option requires an `id`, the implementation should normally derive it from this identifier rather than forcing the user to duplicate it in the value.

For example:

```text
drive.system = file="system.qcow2",format=qcow2,if=none
```

maps naturally to the semantic equivalent of:

```text
-drive file=system.qcow2,format=qcow2,if=none,id=system
```

The implementation must not silently rename duplicate object IDs.

---

## 5. QEMU value coverage

QMX is QEMU-native. For mapped parameter families, QMX should pass values to QEMU's existing parsers/configuration objects wherever practical rather than maintaining an independent whitelist.

Therefore, a value supported by the running QEMU build for a mapped family should generally be expressible through QMX without a grammar revision.

Initial required families include at least:

```text
name
machine
memory
accel
cpu
display
vga
boot
audiodev.<id>
drive.<id>
device.<id>
fw_cfg.<id>
nvram
```

Additional QEMU families may be added using the same grammar.

The grammar does not imply defaults. For example:

```text
cpu = pentium3
```

selects Pentium III only because that line is present. Omitting `cpu` leaves CPU selection to normal QEMU behavior.

The same rule applies to memory, accelerator, machine, display, VGA, audio, boot order, devices, and all other QMX settings.

---

## 6. Relative paths

Every relative filesystem path originating in a QMX setting must be resolved relative to the directory containing the QMX file, never relative to QEMU's process working directory.

Example layout:

```text
Windows98/
    machine.qmx
    state/
        machine.cmos
    disks/
        windows98se.qcow2
    media/
        Windows98_SE.iso
```

QMX:

```text
drive.system = file="disks/windows98se.qcow2",format=qcow2,if=none
drive.cd = file="media/Windows98_SE.iso",format=raw,media=cdrom,if=none,readonly=on
nvram = file="state/machine.cmos",format=cmos128,rtc_init=time0
```

Absolute paths remain absolute.

Path-valued properties must be identified by the semantic adapter for the relevant QEMU family; arbitrary string properties must not be rewritten merely because they resemble a path.

---

## 7. Conversion model

QMX is intentionally close to a simple intermediate representation.

Example:

```text
machine = pc,acpi=off
memory = 384M
cpu = pentium3
drive.os = file="win98.qcow2",format=qcow2,if=none
device.os = ide-hd,drive=os,bus=ide.0,unit=0
```

The parsed representation can be modeled as records such as:

```text
(machine, null, "pc,acpi=off")
(memory, null, "384M")
(cpu, null, "pentium3")
(drive, "os", "file=win98.qcow2,format=qcow2,if=none")
(device, "os", "ide-hd,drive=os,bus=ide.0,unit=0")
```

This structure is deliberately suitable for conversion to:

- QEMU command-line arguments;
- libvirt domain XML;
- Proxmox VM configuration;
- other management-layer formats;
- future QMX editing or validation tools.

QMX itself must not construct or execute shell command text internally. The QEMU implementation should translate parsed records into QEMU's existing option/configuration objects directly.

---

## 8. Persistent legacy CMOS state

Persistent legacy BIOS state is separate from the QMX configuration language.

QMX v0.2 defines:

```text
nvram = file="machine.cmos",format=cmos128,rtc_init=time0
```

`format=cmos128` means an exact 128-byte backing image corresponding byte-for-byte to legacy PC CMOS RAM.

This binary layout is intentionally compatible with the conventional 128-byte CMOS image used by Bochs, but the QMX text format is not a Bochs configuration format.

On compatible x86 PC machine types, QEMU should attach this backing file to the existing MC146818-compatible RTC/CMOS implementation rather than creating a new guest-visible device.

`rtc_init=time0` means configuration bytes are loaded from the file while current RTC date/time is initialized from QEMU's configured virtual clock base.

`rtc_init=image` means RTC date/time bytes are loaded from the image as well.

Guest writes to CMOS must update the emulated CMOS normally. Modified persistent state should be flushed promptly and on normal QEMU shutdown so firmware changes do not depend on a clean guest OS shutdown.

If a writable `cmos128` file does not exist, QEMU should create it from the machine's initialized CMOS defaults rather than from arbitrary zero bytes.

If persistent CMOS cannot be opened, QMX should warn and continue with volatile CMOS unless the format later gains an explicit required-state policy.

BIOS firmware must not rewrite the QMX file.

---

## 9. Media failure policy

Media declared through QMX uses tolerant startup behavior by default.

A disk, CD-ROM, floppy, or similar backing file that is missing or inaccessible must not prevent the rest of the VM from starting merely because the media cannot be opened.

At minimum, these conditions are non-fatal for QMX-defined media:

- file not found;
- access denied;
- removable host device absent;
- backing file cannot currently be opened;
- configured image format cannot open or validate the image;
- initial host I/O failure while opening the medium.

QEMU must emit a clear warning identifying the QMX object, resolved path, error reason, and action taken, then omit the affected device or leave its explicitly configured slot empty.

Example:

```text
QMX warning: drive.cd: cannot open 'D:\VMs\Win98\media\Windows98_SE.iso': No such file or directory; device.cdrom will not be created
```

The implementation must not move a failed device to another bus or slot.

This tolerant behavior applies to media originating from QMX. Existing command-line QEMU failure behavior remains unchanged.

Configuration errors remain fatal, including malformed QMX, duplicate keys, invalid syntax, impossible topology, and values rejected by the relevant QEMU subsystem when they are not merely media-availability failures.

---

## 10. Windows 98 acceptance fixture

The following QMX is a required initial acceptance case, not a set of defaults:

```text
qmx = 1
name = "Windows 98 SE"

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

This must represent the semantic equivalent of the corresponding QEMU CLI settings. Other valid values supported by the running QEMU build for these mapped families are not restricted to the values shown here.

---

## 11. Versioning

QMX files declare their major format version as a normal scalar assignment:

```text
qmx = 1
```

Version `1` is the initial on-disk grammar described by this draft specification.

A parser must reject unsupported major versions.

The specification document may advance through draft revisions without changing `qmx = 1` when those revisions only clarify the same compatible grammar.

---

## 12. Non-goals

QMX v0.2 does not attempt to:

- be a Bochs configuration file;
- provide universal configuration compatibility across emulators;
- embed shell commands;
- replace libvirt or Proxmox;
- store mutable BIOS state directly in the QMX file;
- redefine QEMU device-model validation;
- impose Windows 98 test-fixture values as defaults.

The 128-byte CMOS backing layout remains intentionally simple and interoperable, but that binary-state compatibility is independent of the QMX configuration grammar.
