Name:           veejay-client
Version:        1.6.0
Release:        1%{?dist}
Summary:        Reloaded, a graphical interface for veejay

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
BuildRequires:  gtk3-devel
BuildRequires:  libX11-devel
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libswresample)
BuildRequires:  SDL2-devel
BuildRequires:  alsa-lib-devel

Requires:       veejay-core%{?_isa} >= 1.6.0
Recommends:     veejay >= 1.6.0

%description
Reloaded is the graphical interface for veejay. This program requires
a running veejay backend.

Features:
* Thin client (TCP/IP)
* MIDI support (auto learning)
* Slick interface designed for live performing
* Tracks multiple veejay servers

%prep
%setup -q -n reloaded-%{version}

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
%{_bindir}/reloaded
%{_mandir}/man1/reloaded.1*
%{_datadir}/reloaded/

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Refresh spec for the GTK+3/SDL2-based
