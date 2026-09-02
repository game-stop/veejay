# Process with automake to produce Makefile.in

SUBDIRS = src man

#
# Add any non autoconf'd files here, extra readmes and other misc 
# info to be copied into the dist
#
EXTRA_DIST = \
 packaging.am \
 autogen.sh\
 depcomp \
 autogen.sh \
 PKGBUILD \
 veejay-eidolon.spec \
 debian

RPM_SPEC_FILE = $(top_srcdir)/veejay-eidolon.spec
DEB_SOURCE_PACKAGE = veejay-eidolon

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

