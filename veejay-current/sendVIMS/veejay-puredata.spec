Name:           veejay-puredata
Version:        1.6.0
Release:        1%{?dist}
Summary:        Pure Data external for controlling Veejay via VIMS

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz
%global debug_package %{nil}

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  perl
BuildRequires:  puredata-devel

Requires:       puredata
Recommends:     veejay >= 1.6.0
Provides:       veejay-sayvims = %{version}-%{release}
Obsoletes:      veejay-sayvims < %{version}-%{release}

%description
sendVIMS is a Pure Data external object that sends VIMS protocol events
to a running Veejay backend and receives its periodic status stream as
a Pd list. Intended for live patches, control surfaces and automation
that trigger Veejay events.

%prep
%setup -q

%build
./configure --prefix=%{_prefix}
%make_build PDINCLUDE=%{_includedir}/pd

%install
%make_install PDINCLUDE=%{_includedir}/pd PREFIX=%{_prefix}

%files
%license COPYING
%doc README.md
%{_libdir}/pd/extra/sendVIMS.pd_linux
%{_datadir}/pd/doc/5.reference/help-sendVIMS.pd

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Initial RPM packaging as veejay-puredata
