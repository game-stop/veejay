# Process with automake to produce Makefile.in

SUBDIRS = bio2jack libhash libvjmsg libvjmem 
SUBDIRS += libvevo libplugger libvjnet libyuv libel libvjaudio vevosample veejay
SUBDIRS += tools man


#
# Add any non autoconf'd files here, extra readmes and other misc 
# info to be copied into the dist
#
EXTRA_DIST = \
 autogen.sh\
 depcomp \
 cpuinfo.sh \
 autogen.sh 

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

## .glade files must be in same directory as pixmaps ?!!
#install-data-local:
#	@$(NORMAL_INSTALL)
#	if test -d $(srcdir)/share; then \
#	 $(mkinstalldirs) $(DESTDIR)$(pkgdatadir); \
#	 for dialog in $(srcdir)/share/*; do \
#	 	if test -f $$dialog; then \
#		 $(INSTALL_DATA) $$dialog $(DESTDIR)$(pkgdatadir); \
#		fi \
#	 done \
#	fi

#gveejay-path.h: gveejay-paths.sh
#	@sh $< > $@.tmp ;
#	if cmp -s $@.tmp $@; then
#		rm $@.tmp;
#	else
#		mv $@.tmp $@ ; echo updated gveejay-paths.h;
#	fi

#dist-hook:
#	rm -f %(distdir)/gveejay-paths.sh \
#	if test -d share; then \
#	  mkdir $(distdir)/dialogs; \
#	 for dialog in share/*; do \
#		if test -f $$dialog; then \
#			cp -p $$dialog $(distdir)/dialogs; \
#		fi \
#		done \
#	fi



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
