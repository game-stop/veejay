Summary: veejay - a visual (video) instrument and video sampler for GNU/Linux
Name: veejay
Version: 0.6.4
Release: 1
Copyright: GNU
Group: Applications/Multimedia
Source: veejay-0.6.4.tar.bz2
BuildRoot: /var/tmp/%{name}-%{version}-root

%description
veejay can be used to manipulate video in a realtime envi-
ronment  i.e.  'VJ'  for visual performances or for (auto-
mated) interactive video installations.  It provides mech-
anisms  for  simple  non-desctructive  editing, loop-based
clip editing (video  sampling)  ,capturing  from  multiple
streams,  direct-to-disk  recording in various formats and
mixing from multiple sources to one.  Also, it can  commu-
nicate  with  other  sound and/or video applications using
the Open Sound Control (or  through  an  inhouse  protocol
called  'VIMS')  

%prep
%setup

%build
cd ffmpeg
CFLAGS="$RPM_OPT_FLAGS" sh ./configure \
  --prefix=/usr \
  --infodir=/usr/share/info \
  --mandir=/usr/share/man \
  --sysconfdir=/etc \
  --disable-cmov-extension --disable-smid-accel
make
cd ..
CFLAGS="$RPM_OPT_FLAGS" sh ./configure \
  --prefix=/usr \
  --infodir=/usr/share/info \
  --mandir=/usr/share/man \
  --sysconfdir=/etc \
  --disable-cmov-extension --disable-smid-accel \
  --with-quicktime=/usr/include/quicktime
make

%install
[ -n "$RPM_BUILD_ROOT" -a "$RPM_BUILD_ROOT" != / ] && rm -rf
"$RPM_BUILD_ROOT"
[ -e "$RPM_BUILD_ROOT" ] || mkdir -m 755 "$RPM_BUILD_ROOT"
[ -e "$RPM_BUILD_ROOT/etc" ] || mkdir -m 755 "$RPM_BUILD_ROOT/etc"
[ -e "$RPM_BUILD_ROOT/usr" ] || mkdir -m 755 "$RPM_BUILD_ROOT/usr"
make install DESTDIR="$RPM_BUILD_ROOT"
find "$RPM_BUILD_ROOT" -not -type d \
| sed -e "s@$RPM_BUILD_ROOT@@" -e 's@\(/man./.*\..\)$@\1.gz@' \
> .filelist

%clean
[ -n "$RPM_BUILD_ROOT" -a "$RPM_BUILD_ROOT" != / ] && rm -rf
"$RPM_BUILD_ROOT"

%pre

%post
[ -x /sbin/ldconfig ] && /sbin/ldconfig || :

%files -f .filelist
%defattr(-, root, root)
%doc doc/*

%changelog
* Thu Oct 28 07:34:00 CEST 2004
- description/summary update + bumped version number
* Tue Sep  7 2004 Niels Elburg <nelburg@looze.net>
- version update for 0.6.3
* Sat Feb  8 2004 Niels Elburg <nelburg@looze.net>
- version update for 0.6.0
* Thu Dec  4 2003 Niels Elburg <nelburg@looze.net>
- version update for 0.5.3
* Sun Oct 26 2003 Niels Elburg <nelburg@looze.net>
- version update for 0.5.2
* Fri Oct 18 2003 Niels Elburg <nelburg@looze.net>
- version update for 0.5.1
* Sat Oct 11 2003 Niels Elburg <nielselburg@yahoo.de>
- version update for 0.5.0
* Sun Sep 14 2003 Niels Elburg <nielselburg@yahoo.de>
- version update for 0.4.9
* Fri Sep 5 2003 Niels Elburg <nielselburg@yahoo.de>
- version update for 0.4.8
* Tue Aug 31 2003 Niels Elburg <nielselburg@yahoo.de>
- version update for 0.4.7
* Tue Aug  7 2003 Niels Elburg <nielselburg@yahoo.de>
- version update for 0.4.5
* Tue Aug  5 2003 Andrew Wood <andrew.wood@ivarch.com>
- first draft of spec file created for version 0.4.4

