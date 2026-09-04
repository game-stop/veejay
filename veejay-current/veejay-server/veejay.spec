Name:           veejay
Version:        1.6.0
Release:        1%{?dist}
Summary:        Visual instrument and realtime video sampler

License:        GPL-2.0-or-later
URL:            http://www.veejayhq.net
Source0:        %{name}-%{version}.tar.gz
%{!?veejay_arch_target:%global veejay_arch_target generic}
%{!?veejay_nvjpeg:%global veejay_nvjpeg no}
%global debug_package %{nil}

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  veejay-core-devel >= 1.6.0
BuildRequires:  glib2-devel
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
BuildRequires:  pkgconfig(libswresample)
BuildRequires:  libxml2-devel
BuildRequires:  freetype-devel
BuildRequires:  fontconfig-devel
BuildRequires:  SDL2-devel
BuildRequires:  libunwind-devel
BuildRequires:  libX11-devel
BuildRequires:  libXinerama-devel
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  gdk-pixbuf2-devel
# Optional codec/transport backends; drop the matching --without-* configure
# flag below if these aren't available in the target repo.
BuildRequires:  libdv-devel
BuildRequires:  liblo-devel
BuildRequires:  jack-audio-connection-kit-devel

Requires:       veejay-core%{?_isa} >= 1.6.0

# CUDA is supplied by NVIDIA rather than Fedora. Explicit -nvjpeg release
# builds link against these libraries, but they cannot be resolved from the
# stock Fedora repositories used to build and install the suite. Users of that
# variant must install the matching NVIDIA CUDA runtime repository packages.
%global __requires_exclude ^(libnvjpeg|libcudart)\.so.*$

%description
With veejay, you can play the video like you would play a piano. The
engine is based upon ffmpeg and the mjpegtools featuring a client-server
model with multiple clients.

Veejay likes the sound of your videos as much as their images: sound is
kept in sync (pitched when needed - trickplay) and delivered to JACK for
possible further processing.

Can cluster to allow a number of machines to work together over the
network (uncompressed streaming, veejay chaining).

nvJPEG hardware MJPEG encode/decode is selected at build time with the
veejay_nvjpeg RPM macro; NDI network video/audio is dlopen()'d at runtime
and opt-in via --with-ndi=DIR at build time.

%package devel
Summary:        Development files for veejay
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       veejay-core-devel%{?_isa} >= 1.6.0

%description devel
Link-time library and pkg-config metadata for utilities built against
the Veejay server library.

%prep
%setup -q

%build
./autogen.sh
%configure --with-arch-target=%{veejay_arch_target} --with-nvjpeg=%{veejay_nvjpeg}
%make_build

%install
%make_install
find %{buildroot} \( -name '*.la' -o -name '*.a' \) -delete
%if 0%{?veejay_ci_verify}
install -D -m 0644 config.h "%{_topdir}/VEEJAY-CONFIG/config.h"
%endif

%files
%license COPYING
%doc README.md doc/
%{_bindir}/veejay
%{_libdir}/libveejay-*.so.*
%{_libdir}/livido-plugins/
%{_datadir}/veejay/
%{_mandir}/man1/veejay.1*

%files devel
%{_libdir}/libveejay.so
%{_libdir}/pkgconfig/veejay.pc

%changelog
* Mon Aug 31 2026 Niels Elburg <nwelburg@gmail.com> - 1.6.0-1
- Refresh spec for the FFmpeg 6, GTK-free, SDL2-based build and the
  new veejay-core split
