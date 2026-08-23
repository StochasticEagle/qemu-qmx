# QEMU-QMX Linux packaging

QEMU-QMX 0.9 beta is packaged as an intentional replacement for the distribution's x86 QEMU system-emulator package.

## Replacement policy

The package name is `qemu-qmx`, but it installs the normal executable names:

```text
/usr/bin/qemu-system-x86_64
/usr/bin/qemu-system-i386
```

It therefore declares the native equivalent of:

- provides `qemu-system-x86`;
- conflicts with `qemu-system-x86`;
- replaces/obsoletes `qemu-system-x86` where supported.

This is deliberately **not** a side-by-side installation. Installing QEMU-QMX replaces the stock x86 system emulator. To return to the distribution build, remove `qemu-qmx` and reinstall the distribution's `qemu-system-x86` package.

Some distributions have meta packages such as `qemu`, `qemu-kvm`, or `qemu-full` that require their split QEMU packages at an exact matching version. The package manager may remove such a meta package when QEMU-QMX replaces `qemu-system-x86`. That is acceptable; QEMU-QMX provides the actual x86 system-emulator capability rather than pretending to be the complete distro split-package set.

## Package scope

The packages build only the i386 and x86_64 system emulators from this source tree. They rely on the distribution for shared runtime data and firmware packages rather than overwriting files owned by those packages.

The build is configured with QMX's required virtualization/display/network baseline:

- x86_64 and i386 softmmu targets;
- KVM and TCG;
- SDL display/audio support;
- libslirp user networking;
- system emulation only;
- no guest-agent or QEMU utility package duplication.

QMX documentation is installed under the distribution documentation directory for `qemu-qmx`.

## Arch Linux / pacman

```text
cd packaging/arch
makepkg -si
```

The `PKGBUILD` fetches the QMX release tag and creates a `qemu-qmx` package that replaces `qemu-system-x86`.

## Fedora / RPM / dnf

Build with the normal RPM workflow, for example:

```text
rpmbuild -ba packaging/rpm/qemu-qmx.spec
```

Install the resulting RPM with `dnf install ./qemu-qmx-*.rpm`. DNF will resolve removal of the conflicting stock x86 emulator package.

## Debian/Ubuntu / apt/dpkg

The `packaging/debian/` directory contains the Debian source-package metadata. Copy or symlink it to `debian/` in a release source checkout, then build with the standard Debian tooling, for example:

```text
DEB_BUILD_OPTIONS=nocheck dpkg-buildpackage -b -uc -us
```

Install the resulting `.deb` through apt so dependencies and replacement semantics are resolved:

```text
sudo apt install ./qemu-qmx_*.deb
```

## Release source

The packaging recipes target QMX version `0.9.0-beta.1` and the corresponding release ref `qmx-v0.9.0-beta.1`.
