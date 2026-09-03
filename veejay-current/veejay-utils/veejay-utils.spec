Name:           veejay-utils
Version:        1.6.0
Release:        1%{?dist}
Summary:        Additional command line utilities for veejay

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
BuildRequires:  veejay-core-devel >= 1.6.0
BuildRequires:  veejay-devel >= 1.6.0
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libswresample)

Requires:       veejay-core%{?_isa} >= 1.6.0
Requires:       veejay%{?_isa} >= 1.6.0
Provides:       sayVIMS = %{version}-%{release}

%description
This package contains sayVIMS, a program to communicate with veejay
over the network or local machine.
See veejay-puredata for the Pure Data (sendVIMS) equivalent.

%prep
%setup -q

%build
./autogen.sh
%configure --with-arch-target=%{veejay_arch_target}
%make_build

%install
%make_install
find %{buildroot} -name '*.la' -delete

%files
%license COPYING
%doc README.md
%{_bindir}/sayVIMS
%{_mandir}/man1/sayVIMS.1*

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging
