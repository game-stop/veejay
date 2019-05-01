# Process with automake to produce Makefile.in
SUBDIRS = src

ACLOCAL_AMFLAGS = -I m4 

EXTRA_DIST = \
 autogen.sh\
 depcomp \
 cpuinfo.sh \
 autogen.sh \
 debian/copyright \
 debian/changelog \
 debian/control \
 debian/rules \
 share/bg.png \
 share/bg_black.png \
 share/bg_blue.png \
 share/bg_green.png \
 share/bg_grey.png \
 share/bg_red.png \
 share/bg_yellow.png \
 share/blackwhite.rc \
 share/button_back.png \
 share/button_bounce.png \
 share/button_dec.png \
 share/button_down.png \
 share/button_fbw.png \
 share/button_ffw.png \
 share/button_gotoend.png \
 share/button_gotostart.png \
 share/button_inc.png \
 share/button_loop.png \
 share/button_looponce.png \
 share/button_mini_decrement.png \
 share/button_mini_increment.png \
 share/button_next.png \
 share/button_pause.png \
 share/button_play.png \
 share/button_playmode.png \
 share/button_prev.png \
 share/button_rand.png \
 share/button_reverse.png \
 share/button_right.png \
 share/button_skip.png \
 share/button_skull.png \
 share/button_text.png \
 share/button_up.png \
 share/fx_entry_off.png \
 share/fx_entry_on.png \
 share/gveejay-default.rc \
 share/gveejay.glade \
 share/gveejay.rc \
 share/gveejay.reloaded.glade \
 share/gveejay.reloaded.css \
 share/high_contrast.rc \
 share/icon_apply.png \
 share/icon_berserk.png \
 share/icon_bezerk.png \
 share/icon_bounce.png \
 share/icon_bug.png \
 share/icon_bundle.png \
 share/icon_cache.png \
 share/icon_clear.png \
 share/icon_clearall.png \
 share/icon_color.png \
 share/icon_connect.png \
 share/icon_copy.png \
 share/icon_crop.png \
 share/icon_cut.png \
 share/icon_disconnect.png \
 share/icon_fadein.png \
 share/icon_fadeout.png \
 share/icon_jack.png \
 share/icon_keybind.png \
 share/icon_keydetach.png \
 share/icon_keyframe.png \
 share/icon_launch.png \
 share/icon_loop.png \
 share/icon_looponce.png \
 share/icon_macroclear.png \
 share/icon_macropause.png \
 share/icon_macroplay.png \
 share/icon_macrorec.png \
 share/icon_mask.png \
 share/icon_minus.png \
 share/icon_new.png \
 share/icon_noloop.png \
 share/icon_open.png \
 share/icon_openlist.png \
 share/icon_openstream.png \
 share/icon_osd.png \
 share/icon_paste.png \
 share/icon_plarge.png \
 share/icon_playmode.png \
 share/icon_plus.png \
 share/icon_psmall.png \
 share/icon_question.png \
 share/icon_record.png \
 share/icon_recordstop.png \
 share/icon_recstop.png \
 share/icon_refresh.png \
 share/icon_render.png \
 share/icon_save.png \
 share/icon_saveas.png \
 share/icon_screenshot.png \
 share/icon_send.png \
 share/icon_skull.png \
 share/icon_stream.png \
 share/icon_trash.png \
 share/icon_video.png \
 share/knob.png \
 share/reloaded_classic.glade \
 share/smooth.rc \
 share/sync.png \
 share/veejay-icon.png \
 share/veejay-logo-small.png \
 share/veejay-logo.png \
 gveejay-paths.sh.in  

DISTCLEANFILES = \
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

## .glade files must be in same directory as pixmaps ?!!
install-data-local:
	@$(NORMAL_INSTALL)
	if test -d $(srcdir)/share; then \
		$(mkinstalldirs) $(DESTDIR)$(pkgdatadir); \
		for dialog in $(srcdir)/share/*; do \
	 		if test -f $$dialog; then \
		 		$(INSTALL_DATA) $$dialog $(DESTDIR)$(pkgdatadir); \
			fi \
		done \
	fi; 



gveejay-path.h: gveejay-paths.sh
	@sh $< > $@.tmp ;
	if cmp -s $@.tmp $@; then
		rm $@.tmp;
	else
		mv $@.tmp $@ ; echo updated gveejay-paths.h;
	fi

dist-hook:
	rm -f $(distdir)/gveejay-paths.sh

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
