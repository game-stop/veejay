Name:           veejay-core
Version:        1.6.0
Release:        1%{?dist}
Summary:        Core runtime library shared by the Veejay video tools

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz
%{!?veejay_arch_target:%global veejay_arch_target generic}
%global debug_package %{nil}

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  glib2-devel
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libswresample)

Requires:       glib2

%description
libveejaycore provides the shared memory, messaging, YUV/pixel and
networking primitives used by veejay, veejay-director, veejay-eidolon
and the veejay command line utilities.

%package devel
Summary:        Development files for veejay-core
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description devel
Headers, link-time library and pkg-config metadata for software built
against veejay-core.

%prep
%setup -q -n veejaycore-%{version}

%build
./autogen.sh
%configure --with-arch-target=%{veejay_arch_target}
%make_build

%install
%make_install
find %{buildroot} \( -name '*.la' -o -name '*.a' \) -delete

%ldconfig_scriptlets

%files
%license COPYING
%doc README.md
%{_libdir}/libveejaycore-*.so.*

%files devel
%{_libdir}/libveejaycore.so
%{_includedir}/veejaycore/
%{_libdir}/pkgconfig/veejaycore.pc

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging
