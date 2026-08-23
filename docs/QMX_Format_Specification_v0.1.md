# QMX Virtual Machine Configuration Format
## Draft Specification v0.1

**Status:** Design draft for implementation in a QEMU fork  
**Extension:** `.qmx`  
**Primary goal:** A first-class, portable virtual-machine definition format for QEMU with a Bochs-compatible core syntax and persistent legacy-PC CMOS state.

---

## 1. Purpose

QMX defines a file-based virtual-machine configuration format that can be launched directly by QEMU:

```text
qemu-system-x86_64 machine.qmx
```

The format is intended to fill the role that a VMware `.vmx` file fills: it describes a virtual machine rather than merely supplying one disk image.

QMX has four primary design goals:

1. **Direct QEMU launch.** A VM should be launchable from one `.qmx` file without a wrapper script.
2. **Bochs portability.** The portable portion of a QMX file uses valid `bochsrc` syntax and semantics wherever practical.
3. **Firmware persistence.** Legacy PC CMOS settings can be backed by a 128-byte file, allowing BIOS Setup changes to survive QEMU process restarts.
4. **Clean separation of state.** The `.qmx` file describes virtual hardware and defaults; mutable firmware state resides in a separate CMOS/NVRAM file.

QMX is not intended to replace QEMU's command-line interface. Command-line options remain available for diagnostics and explicit temporary overrides.

---

## 2. Conformance profiles

A QMX file may target one of three profiles.

### 2.1 Portable profile

A **Portable QMX** file contains only directives that are valid Bochs configuration syntax and have a defined QEMU mapping.

A Portable QMX file should be usable as a Bochs configuration file, subject to device/image-format availability.

The file may contain `#qmx:` and `#qemu:` pragmas because they are comments from Bochs' point of view.

```text
#qmx: version=1
#qmx: profile=portable
```

### 2.2 QEMU profile

A **QEMU QMX** file may use QEMU-only pragmas or QEMU-only image formats. It remains syntactically close to `bochsrc`, but direct execution by Bochs is not guaranteed to reproduce the same VM.

```text
#qmx: version=1
#qmx: profile=qemu
```

### 2.3 Bochs profile

A **Bochs QMX** file may contain Bochs directives that QEMU does not implement. QEMU must report unsupported directives rather than silently ignoring them unless explicitly requested with a permissive mode.

---

## 3. File format and grammar

### 3.1 Encoding

QMX files are UTF-8 text. ASCII-only files are naturally valid UTF-8.

A UTF-8 BOM is discouraged.

### 3.2 Basic directive grammar

The portable grammar follows `bochsrc`:

```text
keyword: value
```

or:

```text
keyword: property=value, property=value
```

Examples:

```text
memory: guest=384, host=384
boot: floppy, disk
ata0-master: type=disk, path="windows98se.raw", mode=flat
```

Portable directive names and property names are lowercase.

### 3.3 Comments

A line whose first non-whitespace character is `#` is a comment.

QMX reserves two comment-pragmas:

```text
#qmx: ...
#qemu: ...
```

Bochs treats these as comments. A QMX-aware QEMU parser interprets them.

### 3.4 Quoting

Double quotes SHOULD be used around strings containing whitespace, commas, `#`, or other delimiter characters:

```text
romimage: file="../firmware/my bios.bin"
```

The QMX parser MUST NOT invoke a shell when interpreting quoted values.

### 3.5 Environment variables

The portable profile permits Bochs-style environment-variable expansion:

```text
romimage: file="$FIRMWARE/seabios.bin"
```

QEMU MUST expand variables directly, without shell evaluation.

An undefined variable is an error in strict mode.

### 3.6 Relative paths

For QEMU, relative paths MUST be resolved relative to the directory containing the `.qmx` file.

This permits a VM directory to be moved as a unit:

```text
Windows98/
    machine.qmx
    machine.cmos
    windows98se.qcow2
    floppyA.img
```

For direct Bochs portability, users should launch Bochs with the QMX directory as its working directory unless the Bochs version in use provides equivalent path handling.

### 3.7 Unknown directives

Default QEMU behavior MUST be strict:

- unknown normal directive: error;
- malformed portable directive: error;
- unknown `#qmx:` pragma: error if required, warning if explicitly marked optional;
- unknown `#qemu:` pragma: warning or error according to pragma definition.

QEMU MUST NOT silently reinterpret an unsupported Bochs directive as a different device.

---

## 4. Invocation

A QMX-aware QEMU implementation SHOULD support:

```text
qemu-system-x86_64 machine.qmx
```

When the sole positional argument ends in `.qmx`, it is interpreted as a QMX VM definition rather than as a raw disk image.

An explicit form SHOULD also be provided:

```text
qemu-system-x86_64 -qmx machine.qmx
```

The explicit form removes ambiguity and is useful to scripts.

### 4.1 Command-line precedence

Recommended precedence:

1. QEMU built-in defaults
2. Portable directives in the QMX file
3. QMX/QEMU pragmas in the QMX file
4. Persistent firmware state such as `cmosimage`
5. Explicit QEMU command-line overrides

An explicit command-line option MUST NOT rewrite the QMX file unless a separate edit/save command is invoked.

---

## 5. QMX metadata pragmas

Metadata is encoded in comments so Bochs can ignore it safely.

### 5.1 Version

```text
#qmx: version=1
```

Version 1 is required for this draft.

A parser MUST reject a newer incompatible major version.

### 5.2 Profile

```text
#qmx: profile=portable
```

Allowed values:

- `portable`
- `qemu`
- `bochs`

### 5.3 Optional VM name

```text
#qmx: name="Windows 98 SE"
```

This is descriptive metadata and does not alter guest hardware.

---

## 6. Persistent CMOS / BIOS settings

### 6.1 Portable directive

QMX adopts the Bochs `cmosimage` directive:

```text
cmosimage: file="machine.cmos", rtc_init=time0
```

The backing file is exactly **128 bytes**, corresponding byte-for-byte to legacy PC CMOS RAM.

### 6.2 QEMU implementation requirement

On x86 PC machine types, a QMX-aware QEMU MUST be able to attach the 128-byte file to the existing MC146818-compatible RTC/CMOS implementation.

This is not a new guest-visible device. SeaBIOS and guest software continue to access the normal CMOS ports and semantics.

Conceptually:

```text
SeaBIOS Setup
      |
      | standard CMOS I/O
      v
MC146818 RTC / CMOS
      |
      v
machine.cmos
```

### 6.3 Persistence semantics

When a writable `cmosimage` is configured:

- the file is loaded when the VM is created;
- guest writes to non-RTC CMOS settings modify the emulated CMOS normally;
- modified CMOS state MUST be written back to the backing file;
- firmware settings MUST not require a clean guest operating-system shutdown in order to persist;
- QEMU SHOULD flush dirty CMOS state promptly and MUST flush it during normal VM shutdown;
- file updates SHOULD be crash-resistant.

This permits BIOS Setup's **Save Changes** operation to persist settings simply by writing normal CMOS bytes.

### 6.4 RTC initialization

QMX follows the Bochs meanings:

```text
cmosimage: file="machine.cmos", rtc_init=time0
```

`rtc_init=time0`:
- load persistent configuration bytes from the CMOS image;
- initialize current RTC date/time from the configured virtual clock base rather than trusting stale clock bytes in the file.

```text
cmosimage: file="machine.cmos", rtc_init=image
```

`rtc_init=image`:
- initialize RTC date/time from the image as well.

For ordinary desktop VMs, `rtc_init=time0` is RECOMMENDED.

### 6.5 File creation

If the specified CMOS image does not exist, QEMU SHOULD support creation of a new 128-byte image when the QMX file requests a writable persistent CMOS backend.

The initial content should be generated from normal machine defaults, not from arbitrary zero bytes.

### 6.6 Separation from QMX

BIOS Setup MUST NOT rewrite `machine.qmx`.

Example:

```text
machine.qmx       virtual hardware and defaults
machine.cmos      mutable BIOS/CMOS settings
windows98se.qcow2 guest disk
```

This mirrors the separation between VM configuration and firmware state used by other virtualization systems.

---

## 7. Portable directive compatibility

The following compatibility table defines the initial QMX v1 target.

| Directive | Bochs | QEMU QMX v1 | Portability notes |
|---|---|---|---|
| `memory` | Native | Required | `guest` maps to guest RAM. QEMU may ignore `host` when it has no equivalent. |
| `megs` | Native | Required | Shorthand for equal guest/host memory in Bochs; maps to QEMU guest RAM. |
| `cpu` | Native | Required subset | QEMU parser maps recognized Bochs CPU models to the nearest defined QEMU model; unsupported models are an error, never silently substituted. |
| `romimage` | Native | Required | `file=` maps to QEMU BIOS image selection. |
| `vgaromimage` | Native | Optional/partial | Supported only when the selected QEMU VGA device accepts an explicit VGA ROM. |
| `vga` | Native | Required subset | `extension=cirrus` required; standard VGA mappings may be semantically approximate. |
| `keyboard` | Native | Optional | Only properties with a clear QEMU equivalent should be mapped. |
| `mouse` | Native | Optional | Backend/UI details are emulator-specific. |
| `clock` | Native | Required subset | `time0=local` and `time0=utc` required. |
| `cmosimage` | Native | **Required** | 128-byte persistent PC CMOS image. |
| `floppya` | Native | Required | Common image/media types and inserted/ejected state. |
| `floppyb` | Native | Required | Same as `floppya`. |
| `ata0` | Native | Required standard layout | Standard PC primary IDE channel only in portable profile. |
| `ata1` | Native | Required standard layout | Standard PC secondary IDE channel only in portable profile. |
| `ata2`, `ata3` | Native | Optional | QEMU mapping depends on added controller hardware. |
| `ata0-master` | Native | Required | Disk or CD-ROM. |
| `ata0-slave` | Native | Required | Disk or CD-ROM. |
| `ata1-master` | Native | Required | Disk or CD-ROM. |
| `ata1-slave` | Native | Required | Disk or CD-ROM. |
| `boot` | Native | Required | Up to three of `floppy`, `disk`, `cdrom`, `network`. |
| `floppy_bootsig_check` | Native | Recommended | Maps to firmware behavior when supported. |
| `com1`–`com4` | Native | Optional | Device presence is portable; host backend is not. |
| `parport1`–`parport2` | Native | Optional | Device presence is portable; host backend is not. |
| `sb16` | Native | Optional | Only if QEMU build provides compatible hardware. |
| `es1370` | Native | Recommended | Device model exists in both ecosystems; backend properties differ. |
| `ne2k` | Native | Optional | Exact NE2000 model/backend mapping must be explicit. |
| `e1000` | Native | Recommended | Device exists in both; host networking backend remains emulator-specific. |
| `pci` | Native | Partial | Generic PCI enablement maps naturally; exact chipset/slot semantics may differ. |
| `usb_uhci` | Native | Optional | Controller/device mapping required. |
| `usb_ohci` | Native | Optional | Machine-dependent. |
| `usb_ehci` | Native | Optional | Controller mapping required. |
| `usb_xhci` | Native | Optional | Controller mapping required. |
| `log` | Native | Optional | May map to QEMU logging only where semantics match. |
| debugger directives | Native | Not portable | Emulator-specific by design. |

---

## 8. CPU model compatibility

Bochs and QEMU use different CPU model names. QMX therefore treats the `cpu:` directive as a semantic mapping rather than passing its model string directly to QEMU.

Initial required mappings for the legacy-PC target should include at least:

| Bochs model | QEMU target |
|---|---|
| `i486dx4` | `486` |
| `pentium` | `pentium` |
| `p2_klamath` | `pentium2` |
| `p3_katmai` | `pentium3` |

Other mappings must be verified against the QEMU build's available CPU models.

If there is no sufficiently compatible model, QEMU MUST report an error rather than silently choosing a newer CPU.

Example:

```text
cpu: model=p3_katmai, count=1
```

A QMX-aware QEMU may map this to its Pentium III model internally.

---

## 9. Memory

Portable forms:

```text
memory: guest=384, host=384
```

or:

```text
megs: 384
```

Values follow Bochs' existing syntax and represent MB.

For QMX documentation and UI, storage and memory unit labels use binary quantities:

- KB = 1024 bytes
- MB = 1024 KB
- GB = 1024 MB
- TB = 1024 GB

A QEMU implementation may use its internal byte-size parsers after converting the QMX value.

---

## 10. BIOS ROM

Portable form:

```text
romimage: file="../firmware/bios.bin"
```

For QEMU this maps to the equivalent of selecting that BIOS image for the machine.

QMX MUST NOT assume that the BIOS image is writable.

Persistent Setup state belongs in `cmosimage` or a future explicitly defined NVRAM mechanism.

---

## 11. VGA

Portable legacy target:

```text
vga: extension=cirrus
```

QEMU mapping:

```text
Cirrus Logic GD5446-compatible VGA
```

A compatible VGA BIOS may be specified for Bochs:

```text
vgaromimage: file="$BXSHARE/VGABIOS-lgpl-latest-cirrus.bin"
```

QEMU may use its built-in/device ROM when no explicit QEMU-compatible ROM override is supplied.

The QMX parser must not assume that a Bochs VGA ROM binary can always be used unmodified by every QEMU VGA implementation.

---

## 12. Floppy devices

Examples:

```text
floppya: 1_44="floppyA.img", status=inserted
floppyb: type=1_44
```

QMX v1 portable media types:

- `160k`
- `180k`
- `320k`
- `360k`
- `720k`
- `1_2`
- `1_44`
- `2_88`
- `image`

`status`:

- `inserted`
- `ejected`

`write_protected=1` MUST result in a read-only QEMU backend.

---

## 13. ATA / ATAPI

### 13.1 Controller layout

Portable QMX v1 standard PC channels:

```text
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15
```

For the `pc`/i440FX-style QEMU machine, these map to the conventional primary and secondary IDE channels.

A portable-profile QMX file using non-standard I/O addresses or IRQs MUST be rejected if QEMU cannot reproduce them accurately.

### 13.2 Devices

Examples:

```text
ata0-master: type=disk, path="disk.raw", mode=flat
ata0-slave: type=cdrom, path="tools.iso", status=inserted
ata1-master: type=cdrom, path="Windows98_SE.iso", status=inserted
```

Supported device types:

- `disk`
- `cdrom`

### 13.3 Disk image formats

True cross-emulator portability depends on image-format support.

**Portable baseline:** raw/Bochs `mode=flat`.

A QEMU QCOW2 disk is not directly portable to Bochs merely because its VM configuration is portable.

QMX therefore distinguishes configuration portability from disk-format portability.

For a QEMU-only QCOW2 disk, use a QEMU profile or a QEMU pragma.

Example:

```text
#qmx: profile=qemu
ata0-master: type=disk, path="windows98se.qcow2"
#qemu: disk-format.ata0-master=qcow2
```

Bochs will ignore the `#qemu:` line, but it will still need a disk format it can actually open if the same file is to be run there.

---

## 14. Boot order

Portable syntax:

```text
boot: floppy, disk, cdrom
```

Bochs permits up to three boot classes:

- `floppy`
- `disk`
- `cdrom`
- `network`

QEMU QMX MUST preserve the order.

The QMX `boot:` directive supplies the machine's configured/default boot order.

If a writable `cmosimage` contains a BIOS Setup boot order, firmware-maintained CMOS state takes precedence for normal boots unless an explicit QEMU command-line boot override is supplied.

The SeaBIOS Setup concept of `None` is a terminator for firmware-selected boot entries; `None` is not added to the portable Bochs `boot:` grammar.

---

## 15. Clock

Required portable examples:

```text
clock: sync=none, time0=local
```

```text
clock: sync=none, time0=utc
```

QEMU MUST map these to its RTC base-time configuration.

Other Bochs synchronization modes may be implemented when a faithful QEMU equivalent exists.

---

## 16. QEMU-only pragmas

QEMU-specific settings are encoded as comments so they do not invalidate the file for Bochs.

The initial pragma namespace should remain deliberately small and structured.

Examples:

```text
#qemu: accel=kvm
#qemu: machine=pc
#qemu: acpi=off
#qemu: disk-format.ata0-master=qcow2
#qemu: display=gtk
```

A QEMU pragma is not a raw shell fragment and MUST NOT be passed to a shell.

### 16.1 fw_cfg pragma

Proposed structured form:

```text
#qemu: fw_cfg.name="opt/org.seabios/setup", string="1"
```

QEMU must validate the fw_cfg name and construct the item internally.

Externally supplied fw_cfg items are read-only to the guest; they are suitable for requesting Setup entry but not for persistent BIOS settings.

---

## 17. Example: portable legacy PC

```text
#qmx: version=1
#qmx: profile=portable
#qmx: name="Legacy PC"

memory: guest=128, host=128
cpu: model=pentium, count=1

romimage: file="bios.bin"
cmosimage: file="machine.cmos", rtc_init=time0

vga: extension=cirrus

floppya: 1_44="floppyA.img", status=inserted

ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15

ata0-master: type=disk, path="disk.raw", mode=flat
ata1-master: type=cdrom, path="install.iso", status=inserted

boot: floppy, disk, cdrom
clock: sync=none, time0=local
```

---

## 18. Example: QEMU Windows 98 VM

This example is intentionally a QEMU profile because QCOW2 is used.

```text
#qmx: version=1
#qmx: profile=qemu
#qmx: name="Windows 98 SE"

#qemu: accel=kvm
#qemu: machine=pc
#qemu: acpi=off

memory: guest=384, host=384
cpu: model=p3_katmai, count=1

romimage: file="bios.bin"
cmosimage: file="windows98se.cmos", rtc_init=time0

vga: extension=cirrus

floppya: 1_44="floppyA.img", status=inserted

ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15

ata0-master: type=disk, path="windows98se.qcow2"
#qemu: disk-format.ata0-master=qcow2

ata1-master: type=cdrom, path="/Data/iso/Windows98_SE.iso", status=inserted

boot: floppy, disk
clock: sync=none, time0=local

#qemu: fw_cfg.name="opt/org.seabios/setup", string="1"
```

Once QMX is implemented, the intended launch is:

```text
qemu-system-x86_64 windows98.qmx
```

---

## 19. Recommended QEMU implementation split

The implementation should be separated into two features.

### 19.1 QMX parser/front end

Responsibilities:

- recognize `.qmx`;
- parse the Bochs-compatible grammar;
- validate the selected conformance profile;
- resolve paths;
- translate portable directives into QEMU machine/device configuration objects;
- apply `#qemu:` pragmas;
- permit explicit command-line overrides.

The parser SHOULD feed QEMU's existing configuration machinery rather than constructing an argv string and recursively re-parsing command-line text.

### 19.2 File-backed PC CMOS

Responsibilities:

- attach a 128-byte backing file to the PC RTC/CMOS device;
- load it on machine creation;
- preserve normal guest-visible CMOS behavior;
- write modified state back safely;
- honor `rtc_init=time0` and `rtc_init=image`.

SeaBIOS requires no host-filesystem code for this model.

---

## 20. Security requirements

A QMX parser MUST:

- never execute shell substitutions;
- never treat configuration values as shell command text;
- validate numeric ranges before creating devices;
- reject duplicate singleton directives unless duplicate behavior is explicitly defined;
- avoid silently falling back to a different CPU, chipset, disk format, or device model;
- make path resolution deterministic;
- report unsupported portable directives clearly.

QEMU-specific pragmas MUST be parsed as typed configuration values, not arbitrary command lines.

---

## 21. Future extensions

Potential later additions include:

- VM UUID;
- explicit SMBIOS identity;
- snapshots and removable-media sets;
- additional chipset mappings;
- USB topology;
- sound-device portability profiles;
- a larger firmware NVRAM/EEPROM mechanism if BIOS settings outgrow legacy 128-byte CMOS;
- schema/introspection command;
- conversion tools between QMX, `bochsrc`, libvirt XML, and QEMU command lines.

A future larger NVRAM mechanism must not consume undefined legacy CMOS bytes simply because they appear unused.

---

## 22. Non-goals for QMX v1

QMX v1 does not attempt to:

- make every QEMU device available in Bochs;
- make every Bochs device available in QEMU;
- make QCOW2 readable by Bochs;
- replace libvirt or Proxmox management layers;
- encode live VM runtime state;
- store mutable BIOS settings inside the QMX text file;
- expose host filesystem access directly to SeaBIOS.

---

## 23. Reference behavior used for this draft

This draft intentionally follows documented Bochs configuration conventions:

- `keyword: property=value` configuration syntax;
- `memory` / `megs`;
- `cpu`;
- `romimage`;
- `vgaromimage`;
- `vga`;
- 128-byte `cmosimage`;
- `floppya` / `floppyb`;
- `ata0` / `ata1`;
- `ataN-master` / `ataN-slave`;
- up-to-three-entry `boot` order.

QEMU implementation assumptions are based on current QEMU system invocation and fw_cfg documentation. In particular, externally supplied fw_cfg items are read-only to the guest and therefore are not the persistent-settings mechanism.

### Primary references

Bochs configuration file documentation:  
https://bochs.sourceforge.io/doc/docbook/user/bochsrc.html

Bochs CPU model documentation:  
https://bochs.sourceforge.io/doc/docbook/user/cpu-models.html

QEMU system invocation documentation:  
https://www.qemu.org/docs/master/system/qemu-manpage.html

QEMU fw_cfg specification:  
https://www.qemu.org/docs/master/specs/fw_cfg.html

---

## 24. Open design decisions before QEMU implementation

The following should be decided before the first QEMU patch series is considered stable:

1. Exact QEMU command-line override precedence for settings also present in persistent CMOS.
2. Whether direct `.qmx` positional recognition is accepted upstream or kept as fork behavior with `-qmx` as the canonical option.
3. Exact CPU-model mapping table and validation rules.
4. The default CMOS flush policy: every write, coalesced immediate writeback, or dirty-on-exit plus explicit flush points.
5. Whether QEMU should create a missing `cmosimage` automatically or require an explicit creation property.
6. Which sound and network devices enter the Portable v1 conformance set.
7. Whether `#qemu:` pragmas remain a closed vocabulary or gain a namespaced generic property syntax.

