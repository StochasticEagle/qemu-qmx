Name:           qemu-qmx
Epoch:          2
Version:        11.1.50
Release:        0.9.0.beta1%{?dist}
Summary:        QEMU x86 system emulator with QMX machine configuration support
License:        GPL-2.0-or-later
URL:            https://github.com/StochasticEagle/qemu-qmx
Source0:        https://github.com/StochasticEagle/qemu-qmx/archive/refs/tags/qemu-qmx-v0.9.0-beta.1.tar.gz

Provides:       qemu-system-x86 = 2:%{version}-%{release}
Provides:       qemu-system-x86-core = 2:%{version}-%{release}
Conflicts:      qemu-system-x86
Conflicts:      qemu-system-x86-core
Obsoletes:      qemu-system-x86 < 2:%{version}-%{release}
Obsoletes:      qemu-system-x86-core < 2:%{version}-%{release}
Requires:       qemu-common
Requires:       seabios-bin
Requires:       seavgabios-bin

BuildRequires:  gcc
BuildRequires:  binutils
BuildRequires:  make
BuildRequires:  glib2-devel
BuildRequires:  git
BuildRequires:  libslirp-devel
BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  pixman-devel
BuildRequires:  pkgconfig
BuildRequires:  python3
BuildRequires:  SDL2-devel
BuildRequires:  zlib-devel

%description
QEMU-QMX is the QEMU x86 system emulator with QMX machine configuration
support. This package intentionally replaces Fedora's qemu-system-x86 and
qemu-system-x86-core packages and installs the standard qemu-system-i386,
qemu-system-x86_64, and qemu-kvm executable names.

%prep
%autosetup -n qemu-qmx-qemu-qmx-v0.9.0-beta.1

%build
make -C roms/seabios

mkdir build-package
cd build-package
../configure \
    --prefix=%{_prefix} \
    --libdir=%{_libdir} \
    --target-list=i386-softmmu,x86_64-softmmu \
    --without-default-features \
    --enable-system \
    --enable-kvm \
    --enable-tcg \
    --enable-sdl \
    --enable-slirp \
    --audio-drv-list=sdl \
    --disable-docs \
    --disable-tools \
    --disable-guest-agent
ninja -v

%check
python3 tests/qmx/test-qmx-parser.py build-package/qemu-system-x86_64
python3 tests/qmx/test-qmx-integration.py build-package/qemu-system-x86_64

%install
install -Dpm0755 build-package/qemu-system-x86_64 \
    %{buildroot}%{_bindir}/qemu-system-x86_64
install -Dpm0755 build-package/qemu-system-i386 \
    %{buildroot}%{_bindir}/qemu-system-i386
ln -s qemu-system-x86_64 %{buildroot}%{_bindir}/qemu-kvm
install -Dpm0644 roms/seabios/out/bios.bin \
    %{buildroot}%{_datadir}/qemu/bios-qmx.bin

for rom in kvmvapic.bin linuxboot.bin linuxboot_dma.bin multiboot.bin multiboot_dma.bin pvh.bin qboot.rom; do
    if test -f pc-bios/$rom; then
        install -Dpm0644 pc-bios/$rom %{buildroot}%{_datadir}/qemu/$rom
    fi
done

install -Dpm0644 QMX_VERSION \
    %{buildroot}%{_docdir}/qemu-qmx/QMX_VERSION
install -Dpm0644 docs/QMX_Format_Specification_v0.2.md \
    %{buildroot}%{_docdir}/qemu-qmx/QMX_Format_Specification_v0.2.md
install -Dpm0644 docs/QMX_Minimum_Implementation_Requirements_v0.1.md \
    %{buildroot}%{_docdir}/qemu-qmx/QMX_Minimum_Implementation_Requirements_v0.1.md
install -Dpm0644 docs/releases/QMX_0.9.0-beta.1.md \
    %{buildroot}%{_docdir}/qemu-qmx/QMX_0.9.0-beta.1.md
install -Dpm0644 COPYING \
    %{buildroot}%{_licensedir}/qemu-qmx/COPYING

%files
%license %{_licensedir}/qemu-qmx/COPYING
%doc %{_docdir}/qemu-qmx/QMX_VERSION
%doc %{_docdir}/qemu-qmx/QMX_Format_Specification_v0.2.md
%doc %{_docdir}/qemu-qmx/QMX_Minimum_Implementation_Requirements_v0.1.md
%doc %{_docdir}/qemu-qmx/QMX_0.9.0-beta.1.md
%{_bindir}/qemu-kvm
%{_bindir}/qemu-system-i386
%{_bindir}/qemu-system-x86_64
%{_datadir}/qemu/bios-qmx.bin
%{_datadir}/qemu/kvmvapic.bin
%{_datadir}/qemu/linuxboot*.bin
%{_datadir}/qemu/multiboot*.bin
%{_datadir}/qemu/pvh.bin
%{_datadir}/qemu/qboot.rom

%changelog
* Sun Aug 23 2026 QEMU-QMX Project <noreply@github.com> - 2:11.1.50-0.9.0.beta1
- Initial QMX 0.9 beta replacement package
