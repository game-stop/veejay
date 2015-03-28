# Process with automake to produce Makefile.in

if HAVE_MJPEGTOOLS
SUBDIRS = 
else
SUBDIRS = mjpegtools 
endif

SUBDIRS += aclib
SUBDIRS += libOSC libhash libvjmsg libvjmem libqrwrap 
SUBDIRS += bio2jack libvevo liblzo libvje libplugger libsample libvjnet libsubsample libyuv libel libstream libsamplerec
SUBDIRS += veejay 
SUBDIRS += livido-plugins
SUBDIRS += man

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 autogen.sh\
 depcomp \
 cpuinfo.sh \
 autogen.sh \
 PKGBUILD \
 doc/veejay-HOWTO.html \
 doc/README.quickstart \
 doc/README.whatis \
 doc/README.performance \
 doc/HowtoPlugins.txt \
 doc/HowtoCache.txt \
 doc/VIMS.txt \
 doc/NET.txt \
 debian/changelog \
 debian/control \
 debian/copyright \
 debian/rules \
 buildinfo.sh 

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

## make rpms
rpm: Makefile
	$(MAKE) dist
	rpmbuild -ta --clean $(PACKAGE)-$(VERSION).tar.gz

ubuntu:
	chmod a+x debian/rules
	debuild -i -us -uc -b

release:
	rm -rf .deps */.deps
	$(MAKE) distcheck

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-`date +%Y-%m-%d`

## make debs
deb: Makefile dist
	-chmod -R +w $(PACKAGE)-$(VERSION)
	rm -rf $(PACKAGE)-$(VERSION)
	tar xzf $(PACKAGE)-$(VERSION).tar.gz
	cd $(PACKAGE)-$(VERSION); dpkg-buildpackage -rfakeroot
