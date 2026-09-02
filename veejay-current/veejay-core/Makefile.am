# Process with automake to produce Makefile.in

SUBDIRS = thirdparty
SUBDIRS += libvjmsg libvjmem 
SUBDIRS += libvevo libvjnet libyuv
SUBDIRS += veejaycore

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 packaging.am \
 autogen.sh\
 depcomp \
 autogen.sh \
 buildinfo.sh \
 PKGBUILD \
 veejay-core.spec \
 debian

RPM_SPEC_FILE = $(top_srcdir)/veejay-core.spec
DEB_SOURCE_PACKAGE = veejay-core

# Include packaging targets
include $(top_srcdir)/packaging.am

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = veejaycore.pc

DISTCLEANFILES = \
	veejaycore.pc \
	config.cache \
	config.status \
	config.log 

MAINTAINERCLEANFILES = \
	compile \
	depcomp \
	install-sh \
	missing \
	mkinstalldirs \
	Makefile.in \
	aclocal.m4 \
	config.guess \
	config.h.in \
	config.sub \
	configure \
	ltmain.sh \
	stamp-h.in

release:
	rm -rf .deps */.deps
	$(MAKE) distcheck

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-`date +%Y-%m-%d`

install-exec-hook:
	@echo "You may need to run 'ldconfig' as root to update the shared library cache."