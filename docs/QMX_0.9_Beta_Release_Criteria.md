# QMX 0.9 Beta Release Criteria

QMX 0.9 beta is the feature-freeze milestone for the initial QMX implementation.

## Required verification

The beta candidate must pass the repository parser and integration regressions:

```text
python tests/qmx/test-qmx-parser.py build/qemu-system-x86_64
python tests/qmx/test-qmx-integration.py build/qemu-system-x86_64
```

The integration suite verifies the following release requirements without requiring a guest operating system:

1. Missing QMX-defined floppy, hard-disk, and CD-ROM media warn and do not prevent QEMU startup. `device.*` objects that reference an unavailable drive are omitted.
2. Relative path-valued QMX settings are resolved against the directory containing the QMX file rather than QEMU's current working directory. Coverage includes drive media, `fw_cfg` files, character-device paths, legacy character-device file paths, object files, and persistent CMOS state.
3. Traditional QEMU command-line options override QMX. Same-ID `drive`, `device`, `audiodev`, `netdev`, `chardev`, and `object` objects are suppressed, and `fw_cfg` conflicts are resolved by `name=`.
4. Persistent CMOS is exercised first with `rtc_init=time0` and then reloaded with `rtc_init=image`; the generated image must remain exactly 128 bytes and contain a valid RTC date/time.
5. `qemu-system-x86_64 machine.qmx` and `qemu-system-x86_64 -qmx machine.qmx` must both complete machine creation successfully for the same QMX configuration.

`-qmx-check machine.qmx` remains the fast parser/translation validation path and does not create a virtual machine.

## Relative path adapters

QMX rebases only fields with defined filesystem-path semantics. The 0.9 implementation covers:

- `bios`
- `nvram.file`
- `drive.<id>.file`
- `fw_cfg.<id>.file`
- `chardev.<id>.path`
- `chardev.<id>.logfile`
- `netdev.<id>.script`
- `netdev.<id>.downscript`
- `netdev.<id>.vhostdev`
- `object.<id>.file`
- `object.<id>.filename`
- `object.<id>.mem-path`
- `serial`, `parallel`, and `monitor` legacy character-device values using `file:`, `pipe:`, or `unix:` path forms

Arbitrary string properties are not rewritten merely because they resemble paths.

## Explicitly out of scope for 0.9 beta

The following are not beta blockers and are intentionally deferred:

- include/import support;
- variables or constants;
- conversion/introspection output that emits canonical QEMU argv or another target format;
- a separate `qmx-info` or dump utility.

The internal QMX representation should remain suitable for future conversion tooling, but no such interface is required for 0.9 beta.

## Release boundary

After a clean build and successful execution of both QMX test scripts, the QMX feature set is considered ready for the 0.9 beta release. Further work before 1.0 should prioritize compatibility testing, diagnostics, and regression fixes rather than new QMX language features.
