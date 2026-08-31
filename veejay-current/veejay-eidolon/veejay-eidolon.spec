Name:           veejay-eidolon
Version:        1.6.0
Release:        1%{?dist}
Summary:        Generative/life-simulation sample source for Veejay

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
BuildRequires:  ffmpeg-devel
BuildRequires:  libX11-devel

Requires:       veejay-core%{?_isa} >= 1.5.67
Recommends:     veejay >= 1.6.0

%description
eidolon is a small standalone tool built on libveejaycore that feeds
generated or evolving frame content into a running veejay backend.

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
%{_bindir}/eidolon

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging
