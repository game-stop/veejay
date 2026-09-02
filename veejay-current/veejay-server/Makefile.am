# Process with automake to produce Makefile.in

SUBDIRS = thirdparty
SUBDIRS += libvjxml
SUBDIRS += libvje libplugger libsample libsubsample libel libstream libsamplerec
SUBDIRS += libveejay
SUBDIRS += veejay 
SUBDIRS += livido-plugins
SUBDIRS += man
SUBDIRS += share

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 packaging.am \
 autogen.sh\
 depcomp \
 autogen.sh \
 PKGBUILD \
 $(PACKAGE).spec \
 doc/veejay-HOWTO.html \
 doc/README.quickstart.md \
 doc/README.whatis.md \
 doc/HOWTO.plugins.md \
 doc/VIMS.md \
 doc/README.network.md \
 debian \
 debian/changelog \
 debian/control \
 debian/copyright \
 debian/rules \
 buildinfo.sh 

# Include packaging targets
include $(top_srcdir)/packaging.am

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = veejay.pc

DISTCLEANFILES = \
	veejay-config.h \
	veejay.pc \
	confdefs.h \
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

ubuntu:
	chmod a+x debian/rules
	debuild -i -us -uc -b

release:
	rm -rf .deps */.deps
	$(MAKE) distcheck

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-`date +%Y-%m-%d`

install-exec-hook:
	@echo "You may need to run 'ldconfig' as root to update the shared library cache."
