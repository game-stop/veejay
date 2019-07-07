SHAPEARCHIVE=shapes.tar.lz

dist_pkgdata_DATA=$(SHAPEARCHIVE)

install-data-hook:
	cd $(DESTDIR)$(pkgdatadir); \
	cat $(SHAPEARCHIVE) | unlzma | tar --list > uninstall_manifest.txt; \
	cat $(SHAPEARCHIVE) | unlzma | tar --no-same-owner --extract; \
	rm --force $(SHAPEARCHIVE)

# todo: uninstall files listed in uninstall_manifest.txt
uninstall-local:
	cd $(DESTDIR)$(pkgdatadir); \
	rm --force uninstall_manifest.txt

DISTCLEANFILES = Makefile.in
MAINTAINERCLEANFILES = Makefile.in
