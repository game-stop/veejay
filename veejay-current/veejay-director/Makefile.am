# Process with automake to produce Makefile.in
SUBDIRS = src man

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 packaging.am \
 autogen.sh\
 depcomp \
 autogen.sh \
 PKGBUILD \
 veejay-director.spec \
 debian

RPM_SPEC_FILE = $(top_srcdir)/veejay-director.spec
DEB_SOURCE_PACKAGE = veejay-director

# Include packaging targets
include $(top_srcdir)/packaging.am

DISTCLEANFILES = \
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

