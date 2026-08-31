Name:           veejay-core
Version:        1.6.0
Release:        1%{?dist}
Summary:        Core runtime library shared by the Veejay video tools

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  glib2-devel
BuildRequires:  ffmpeg-devel

Requires:       glib2
Requires:       ffmpeg-libs

%description
libveejaycore provides the shared memory, messaging, YUV/pixel and
networking primitives used by veejay, veejay-director, veejay-eidolon
and the veejay command line utilities.

%prep
%setup -q

%build
./autogen.sh
%configure
%make_build

%install
%make_install
find %{buildroot} -name '*.la' -delete

%ldconfig_scriptlets

%files
%license COPYING
%doc README.md
%{_libdir}/libveejaycore-*.so.*
%{_libdir}/libveejaycore.so
%{_includedir}/veejaycore/
%{_libdir}/pkgconfig/veejaycore.pc

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging
