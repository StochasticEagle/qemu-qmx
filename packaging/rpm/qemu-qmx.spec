Name:           qemu-qmx
Epoch:          2
Version:        11.1.50
Release:        0.9.0.beta1%{?dist}
Summary:        QEMU x86 system emulator with QMX machine configuration support
License:        GPL-2.0-or-later
URL:            https://github.com/StochasticEagle/qemu-qmx
Source0:        https://github.com/StochasticEagle/qemu-qmx/archive/refs/tags/qemu-qmx-v0.9.0-beta.1.tar.gz
Source1:        https://github.com/StochasticEagle/seabios-setupmenu/archive/e0b009af38836aa5d1d55713d470721835f03664.tar.gz

Provides:       qemu-system-x86 = 2:%{version}-%{release}
Provides:       qemu-system-x86-core = 2:%{version}-%{release}
Provides:       qemu-common = 2:%{version}-%{release}
Conflicts:      qemu-system-x86
Conflicts:      qemu-system-x86-core
Conflicts:      qemu-common
Obsoletes:      qemu-system-x86 < 2:%{version}-%{release}
Obsoletes:      qemu-system-x86-core < 2:%{version}-%{release}
Obsoletes:      qemu-common < 2:%{version}-%{release}

BuildRequires:  gcc
BuildRequires:  binutils
BuildRequires:  make
BuildRequires:  glib2-devel
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
support. This package intentionally replaces the distribution QEMU x86
system-emulator/runtime packages and installs the standard executable and
firmware filenames.

%prep
%autosetup -n qemu-qmx-qemu-qmx-v0.9.0-beta.1
rm -rf roms/seabios
mkdir -p roms/seabios
tar -xzf %{SOURCE1} --strip-components=1 -C roms/seabios

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
DESTDIR=%{buildroot} ninja -C build-package install
install -Dpm0644 roms/seabios/out/bios.bin %{buildroot}%{_datadir}/qemu/bios.bin
install -Dpm0644 roms/seabios/out/bios.bin %{buildroot}%{_datadir}/qemu/bios-256k.bin
install -Dpm0644 QMX_VERSION %{buildroot}%{_docdir}/qemu-qmx/QMX_VERSION
install -Dpm0644 docs/QMX_Format_Specification_v0.2.md %{buildroot}%{_docdir}/qemu-qmx/QMX_Format_Specification_v0.2.md
install -Dpm0644 docs/QMX_Minimum_Implementation_Requirements_v0.1.md %{buildroot}%{_docdir}/qemu-qmx/QMX_Minimum_Implementation_Requirements_v0.1.md
install -Dpm0644 docs/releases/QMX_0.9.0-beta.1.md %{buildroot}%{_docdir}/qemu-qmx/QMX_0.9.0-beta.1.md
install -Dpm0644 COPYING %{buildroot}%{_licensedir}/qemu-qmx/COPYING

%files
%license %{_licensedir}/qemu-qmx/COPYING
%doc %{_docdir}/qemu-qmx/
%{_bindir}/qemu-system-i386
%{_bindir}/qemu-system-x86_64
%{_datadir}/qemu/

%changelog
* Sun Aug 23 2026 QEMU-QMX Project <noreply@github.com> - 2:11.1.50-0.9.0.beta1
- Initial QEMU-QMX 0.9 beta replacement package
