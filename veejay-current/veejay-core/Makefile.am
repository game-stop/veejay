# Process with automake to produce Makefile.in

SUBDIRS = thirdparty
SUBDIRS += libvjmsg libvjmem 
SUBDIRS += libvevo libvjnet libyuv
SUBDIRS += veejaycore

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 autogen.sh\
 depcomp \
 cpuinfo.sh \
 autogen.sh \
 buildinfo.sh 

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

## make rpms
rpm: Makefile
	$(MAKE) dist
	rpmbuild -ta --clean $(PACKAGE)-$(VERSION).tar.gz

release:
	rm -rf .deps */.deps
	$(MAKE) distcheck

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-`date +%Y-%m-%d`
