# QMX Requirements and Release Criteria

This document defines implementation and release requirements. The language
contract is in `qmx-format-specification.md`.

## Compatibility boundary

QMX is additive. Without a QMX file, QEMU must retain upstream argument
processing, defaults, firmware lookup, device creation, and failure behavior.
The implementation must be isolated from QEMU subsystems apart from the small
hooks needed to recognize a QMX launch, initialize QMX runtime state, and
apply the documented tolerant-media policy.

QMX must not maintain a parallel catalog of QEMU parameters. It must obtain
parameter spelling and argument arity from QEMU's generated option definitions
and send values through QEMU's existing option-processing path. Consequently,
a newly added QEMU parameter becomes expressible in QMX without a QMX parser
change unless it needs QMX-specific path or identity semantics.

The QMX parser must not invoke a shell.

## Required behavior

The implementation must support:

- direct `.qmx`, `-qmx`, and `-qmx-check` invocation;
- QEMU parameters with values and argumentless QEMU switches;
- any number of repeatable parameters distinguished by occurrence names;
- ID derivation for QEMU object parameters, including TPM backends;
- command-line precedence for singleton parameters, same-ID objects, and
  same-name `fw_cfg` entries;
- VM-relative paths, `fat:` directory paths, absolute paths, explicit
  `./` paths, and system-firmware search paths;
- required read-only pflash firmware, first-run writable pflash templates,
  and tolerant ordinary QMX media;
- QEMU-owned startup and shutdown of software TPM helpers selected by QMX; and
- the documented persistent CMOS facility on compatible machines.

`-qmx-check` must reject malformed syntax, duplicate keys and properties,
unsupported parameter names, wrong argumentless-switch values, invalid object
IDs, missing required system firmware, and invalid QMX-specific metadata. QEMU
continues to own target-, machine-, device-, and property-level validation
during normal initialization.

## QEMU QMX Builder

The graphical QEMU QMX Builder is a QEMU component under `tools/qmx-builder`, built
and installed by QEMU's Meson build. It remains a separate application from
the QEMU system emulator and may maintain a curated, versioned option catalog
for forms, descriptions, discovery, and validation. That catalog is a GUI
aid; it is not part of the QMX language and must not become a dependency of
QEMU's QMX parser.

The builder must write ordinary human-editable QMX and preserve fields it does
not understand when editing a file created by a newer QEMU or another tool.

## Automated verification

A release candidate must pass from a clean build:

```text
python3 tests/qmx/test-qmx-parser.py build/qemu-system-x86_64
python3 tests/qmx/test-qmx-integration.py build/qemu-system-x86_64
```

Automated coverage must verify:

1. both launch forms and `-qmx-check`;
2. general QEMU option discovery from QEMU's own generated definitions;
3. valued parameters, argumentless switches, repeatable options, and ID
   derivation, including TPM and pflash forms;
4. syntax errors, duplicate assignments and properties, and ID mismatch;
5. scalar, same-ID, and `fw_cfg` command-line precedence;
6. VM-relative paths, `./`, absolute paths, `fat:`, configured firmware
   search, writable pflash initialization, and a missing required firmware
   error;
7. direct software TPM startup, socket readiness, and cleanup when QEMU exits;
8. tolerant missing-media handling and dependent-device omission;
9. persistent CMOS creation, reload, and exact 128-byte size; and
10. successful machine creation without a guest operating system.

Release artifacts must install the current format and requirements documents
under their actual repository filenames. Distribution and release builds must
configure firmware search directories appropriate for their installed system
layout and retain QEMU's standard data directory.
