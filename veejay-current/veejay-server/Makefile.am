# Process with automake to produce Makefile.in

SUBDIRS = mjpegtools aclib bio2jack libOSC libhash libvjmsg libvjmem 
SUBDIRS += libvevo liblzo libgoom libvje libsample libvjnet libyuv libel libstream liblavjpeg libsamplerec
SUBDIRS += veejay 
SUBDIRS += man

EXTRA_DIST = \
 autogen.sh\
 depcomp \
 cpuinfo.sh \
 autogen.sh \
 gveejay-paths.sh.in  

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = veejay.pc

DISTCLEANFILES = \
	veejay-config.h \
	veejay.pc \
	confdefs.h \
	config.cache \
	config.status \
	config.log \
	gveejay-paths.h

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

## make debs
deb: Makefile dist
	-chmod -R +w $(PACKAGE)-$(VERSION)
	rm -rf $(PACKAGE)-$(VERSION)
	tar xzf $(PACKAGE)-$(VERSION).tar.gz
	cd $(PACKAGE)-$(VERSION); dpkg-buildpackage -rfakeroot
