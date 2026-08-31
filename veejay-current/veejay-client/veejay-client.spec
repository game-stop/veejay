Name:           veejay-client
Version:        1.6.0
Release:        1%{?dist}
Summary:        Reloaded, a graphical interface for veejay

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  veejay-core-devel >= 1.5.67
BuildRequires:  gtk3-devel
BuildRequires:  libX11-devel
BuildRequires:  ffmpeg-devel
BuildRequires:  SDL2-devel
BuildRequires:  alsa-lib-devel

Requires:       veejay-core%{?_isa} >= 1.5.67
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
%setup -q

%build
./autogen.sh
%configure
%make_build

%install
%make_install
find %{buildroot} -name '*.la' -delete

%files
%license COPYING
%doc README.md
%{_bindir}/reloaded
%{_mandir}/man1/reloaded.1*

%changelog
+ Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Refresh spec for the GTK+3/SDL2-based
