Name:           veejay-director
Version:        1.6.0
Release:        1%{?dist}
Summary:        Graphical remote-control and mesh director for Veejay

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz
%{!?veejay_arch_target:%global veejay_arch_target generic}
%global debug_package %{nil}

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  veejay-core-devel >= 1.6.0
BuildRequires:  gtk3-devel
BuildRequires:  libX11-devel
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libswresample)
BuildRequires:  SDL2-devel

Requires:       veejay-core%{?_isa} >= 1.6.0
Recommends:     veejay >= 1.6.0

%description
veejay-director is a GTK+3 client used to steer one or more running
veejay backends, arrange video-wall meshes, manage presets and stream
to/from NDI sources. NDI support is dlopen()'d at runtime; build with
NDI_SDK_DIR pointing at an installed NDI SDK to enable it.

This program requires a running veejay backend to control.

%prep
%setup -q -n reloaded-%{version}

%build
./autogen.sh
# NDI is dlopen()'d at runtime by director-ndi.c, so only the SDK header is
# needed to build against it. The SDK install location has no portable
# convention, so this reads NDI_SDK_DIR (defaulting to this build host's
# known location) and only passes --with-ndi when the header is actually
# there; otherwise configure's own "auto" detection leaves NDI disabled.
NDI_SDK_DIR="${NDI_SDK_DIR:-/opt/ndi6}"
if [ -f "$NDI_SDK_DIR/include/Processing.NDI.Lib.h" ]; then
  %configure --with-arch-target=%{veejay_arch_target} --with-ndi="$NDI_SDK_DIR"
else
  %configure --with-arch-target=%{veejay_arch_target}
fi
%make_build

%install
%make_install
find %{buildroot} -name '*.la' -delete

%files
%license COPYING
%doc README.md
%{_bindir}/veejay-director
%{_mandir}/man1/veejay-director.1*
%{_datadir}/applications/org.veejay.Director.desktop

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging
