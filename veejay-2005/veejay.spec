# norootforbuild
# neededforbuild update-desktop-files gtk2-devel-packages glib2 gtk2 libglade3 libdv libxml2 jack DirectFB SDL libjpeg freetype2 gtk2-devel glib2-devel libglade-devel libxml2-devel jack-devel pkgconfig SDL-devel libjpeg-devel freetype2-devel valgrind alsa-devel alsa slang-devel slang libstdc++-devel libstdc++ zlib-devel zlib

%define name    veejay
%define	version 0.8
%define release 0.oc2pus.1
%define prefix  /usr

%define builddir $RPM_BUILD_DIR/%{name}-%{version}

Summary:       Video Mixing/Editing Instrument
Name:          %{name}
Version:       %{version}
Release:       %{release}
Prefix:        %{prefix}
Copyright:     LGPL
URL:           http://veejay.sourceforge.net/
Packager:      oc2pus <oc2pus@arcor.de>
Group:         Productivity/Multimedia/Video/Editors and Convertors
Source:        %{name}-%{version}.tar.bz2
Patch0:        %{name}-configure.patch
BuildRoot:     %{_tmppath}/%{name}-%{version}-build
Prereq:        /sbin/ldconfig
Requires:      libdv >= 0.102
Requires:      libxml2 >= 2.5.4
Requires:      jack >= 0.98
Requires:      DirectFB >= 0.9.17
Requires:      SDL >= 1.2.3
Requires:      libjpeg
Requires:      freetype2
Requires:      alsa
Requires:      slang
Requires:      libstdc++
Requires:      zlib
Requires:      gtk2
Requires:      glib2
Requires:      libglade2
Requires:      libxml2 >= 2.5.4
BuildRequires: libdv >= 0.102
BuildRequires: libxml2-devel >= 2.5.4
BuildRequires: jack-devel >= 0.98
BuildRequires: pkgconfig
BuildRequires: DirectFB >= 0.9.17
BuildRequires: SDL-devel >= 1.2.3
BuildRequires: libjpeg-devel
BuildRequires: freetype2-devel
BuildRequires: alsa-devel
BuildRequires: slang-devel
BuildRequires: libstdc++-devel
BuildRequires: zlib-devel
#BuildRequires: gtk2-devel >= 2.6
#BuildRequires: glib2-devel >= 2.6
#BuildRequires: libglade2-devel >= 2.4
BuildRequires: gtk2-devel
BuildRequires: glib2-devel
BuildRequires: libglade2-devel
BuildRequires: valgrind
Obsoletes:     gveejay

%description
Veejay is a visual instrument and realtime video sampler.
It allows you to 'play' the video like you would play a Piano and it allows
you to record the resulting video directly to disk for immediate playback (video sampling).

Veejay is beeing developed in the hope it will be usefull for VJ's, 
media artists and other interested users that want to use Free Software for
their performances and/or video installations.

Below is a list of key features:
+ realtime video mixing from multiple sources
+ native YUV processing
+ loop based 'sample' editing
+ non destructive editing
+ chained fx editing
+ dynamic keyboard mappings
+ direct to disk recording
+ networking support
+ GUI/engine are seperated programs 
+ many effects (>110)
 

Author: Niels Elburg <nelburg@looze.net>

Requires: gtk2
Requires: glib2
Requires: libglade2
Requires: libxml2 >= 2.5.4

%prep
%setup -q -n %{name}-%{version}
%patch0 -p1

./autogen.sh
%{?suse_update_config:%{suse_update_config -f}}

CFLAGS="$RPM_OPT_FLAGS" \
CXXFLAGS="$RPM_OPT_FLAGS" \
./configure \
	--prefix=%{prefix} \
	--mandir=%{_mandir}
#	--without-gui

%build
# Setup for parallel builds
numprocs=`egrep -c ^cpu[0-9]+ /proc/stat || :`
if [ "$numprocs" = "0" ]; then
	numprocs=1
fi
make -j$numprocs

%install
[ -d %{buildroot} -a "%{buildroot}" != "" ] && rm -rf %{buildroot}

make install-strip \
	DESTDIR=%{buildroot}

# remove static libs
rm -f %{buildroot}%{_libdir}/lib*.a
rm -f %{buildroot}%{_libdir}/lib*.la

# icon and menu-entry
mkdir -p %{buildroot}%{_datadir}/pixmaps
install -m 644 share/%{name}-logo.png %{buildroot}%{_datadir}/pixmaps/gveejay.png
cat > gveejay.desktop << EOF
[Desktop Entry]
Comment=Video Mixing/Editing Instrument
Exec=gveejay
GenericName=
Icon=%{name}
MimeType=
Name=gveejay
Path=
StartupNotify=true
Terminal=false
Type=Application
EOF
%suse_update_desktop_file -i gveejay AudioVideo Player

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%clean
[ -d %{buildroot} -a "%{buildroot}" != "" ] && rm -rf %{buildroot}

%files
%defattr(-, root, root)
%doc AUTHORS BUGS COPYING ChangeLog INSTALL README*
%doc doc
%{_bindir}/%{name}
%{_bindir}/any2yuv
%{_bindir}/rawdv2yuv
%{_bindir}/sayVIMS
%{_bindir}/yuv2rawdv
%{_datadir}/%{name}/*.png
%{_libdir}/*.so*
%{_libdir}/pkgconfig/%{name}.pc
%{_mandir}/man1/*
# gveejay
%doc gveejay/AUTHORS
%{_bindir}/gveejay
%{_datadir}/%{name}/gveejay.glade
%{_datadir}/%{name}/gveejay.rc
%{_datadir}/pixmaps/gveejay.png
%{_datadir}/applications/gveejay.desktop

%changelog -n veejay
* Sat Apr 23 2005 - oc2pus@arcor.de 0.8-0.oc2pus.1
- update to 0.8
- patched configure for using glib2-2.4.*, gtk2-2.4.*, libglade2-2.2.*
- patched gveejay sources for using gtk2-2.4
* Fri Apr 15 2005 - oc2pus@arcor.de 0.7.3-0.oc2pus.2
- directfb re-enabled
* Mon Mar 21 2005 - oc2pus@arcor.de 0.7.3-0.oc2pus.1
- initial rpm
- directfb disabled, doesn't compile
