all: pd_linux

include Makefile.defs

VERSION = 1.6.0

PACKAGE ?= veejay-puredata
DEB_SOURCE_PACKAGE ?= veejay-puredata
RPM_SPEC_FILE ?= veejay-puredata.spec
PACKAGING_BUILD_DIR ?= packaging-build
PACKAGING_DIST_LOCK ?= .packaging-dist.lock
FLOCK ?= flock
SBUILD ?= sbuild
SBUILD_CREATECHROOT ?= sbuild-createchroot
SBUILD_CREATE_ARGS ?=
SBUILD_CHROOT_MODE ?= schroot
SBUILD_DIST ?= unstable
SBUILD_ARCH ?= $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
SBUILD_MIRROR ?= http://deb.debian.org/debian
SBUILD_UNSHARE_TARBALL ?= $(HOME)/.cache/sbuild/$(SBUILD_DIST)-$(SBUILD_ARCH).tar.zst
SBUILD_CREATE_DIR ?= /tmp/sbuild-create-$(SBUILD_DIST)-$(SBUILD_ARCH)-$(shell id -u 2>/dev/null || echo user)
SBUILD_CHROOT ?= $(if $(filter unshare,$(SBUILD_CHROOT_MODE)),$(SBUILD_UNSHARE_TARBALL),$(SBUILD_DIST)-$(SBUILD_ARCH)-sbuild)
SBUILD_ARGS ?=
SBUILD_LOCAL_PACKAGE_DIR ?=
SBUILD_LOCAL_PACKAGES = $(wildcard $(SBUILD_LOCAL_PACKAGE_DIR)/*.deb)
SBUILD_LOCAL_PACKAGE_ARG = $(if $(strip $(SBUILD_LOCAL_PACKAGES)),--extra-package=$(SBUILD_LOCAL_PACKAGE_DIR))
ARCH_CHROOT_DIR ?= /var/lib/archbuild/extra-x86_64
ARCH_LOCAL_PACKAGE_DIR ?=
ARCH_LOCAL_PACKAGES = $(filter-out %.sig,$(wildcard $(ARCH_LOCAL_PACKAGE_DIR)/*.pkg.tar.*))
ARCH_LOCAL_PACKAGE_ARGS = $(foreach package,$(ARCH_LOCAL_PACKAGES),-I $(package))
MOCK ?= mock
MOCK_CONFIG ?= fedora-rawhide-x86_64
MOCK_ARGS ?=
MOCK_LOCAL_PACKAGE_DIR ?=
MOCK_LOCAL_PACKAGES = $(filter-out %.src.rpm,$(wildcard $(MOCK_LOCAL_PACKAGE_DIR)/*.rpm))

.PHONY: all clean install dist deb arch rpm clean-packaging check-sbuild check-flock sbuild

check-flock:
	@command -v "$(FLOCK)" >/dev/null 2>&1 || { \
		echo "ERROR: '$(FLOCK)' not found. Install util-linux or pass FLOCK=/path/to/flock."; \
		exit 127; \
	}

sbuild:
	@command -v "$(SBUILD_CREATECHROOT)" >/dev/null 2>&1 || { \
		echo "ERROR: '$(SBUILD_CREATECHROOT)' not found. Install sbuild or pass SBUILD_CREATECHROOT=/path/to/sbuild-createchroot."; \
		exit 127; \
	}
	@if test "$(SBUILD_CHROOT_MODE)" != "unshare"; then \
		echo "ERROR: make sbuild creates unshare tarball chroots; run with SBUILD_CHROOT_MODE=unshare."; \
		exit 1; \
	fi
	@if test -e "$(SBUILD_CHROOT)"; then \
		echo "==> sbuild unshare chroot already exists: $(SBUILD_CHROOT)"; \
	else \
		echo "==> Creating sbuild unshare chroot $(SBUILD_CHROOT) for $(SBUILD_DIST)/$(SBUILD_ARCH)..."; \
		rm -rf "$(SBUILD_CREATE_DIR)"; \
		mkdir -p "$$(dirname "$(SBUILD_CHROOT)")" "$$(dirname "$(SBUILD_CREATE_DIR)")"; \
		$(SBUILD_CREATECHROOT) --chroot-mode=unshare --arch=$(SBUILD_ARCH) $(SBUILD_CREATE_ARGS) \
			--make-sbuild-tarball="$(SBUILD_CHROOT)" \
			$(SBUILD_DIST) "$(SBUILD_CREATE_DIR)" "$(SBUILD_MIRROR)"; \
	fi

check-sbuild:
	@command -v "$(SBUILD)" >/dev/null 2>&1 || { \
		echo "ERROR: '$(SBUILD)' not found. Install sbuild or pass SBUILD=/path/to/sbuild."; \
		exit 127; \
	}
	@if test "$(SBUILD_CHROOT_MODE)" = "schroot" && ! id -nG | tr ' ' '\n' | grep -qx sbuild; then \
		echo "ERROR: user '$$(id -un)' is not an effective member of the sbuild group."; \
		echo "Run: sudo sbuild-adduser $$(id -un), then log out/in or run: newgrp sbuild"; \
		exit 1; \
	fi
	@if test "$(SBUILD_CHROOT_MODE)" = "unshare" && test -n "$(SBUILD_CHROOT)" && \
	   test ! -e "$(SBUILD_CHROOT)" && test ! -e "$${HOME}/.cache/sbuild/$(SBUILD_CHROOT)"; then \
		echo "ERROR: unshare chroot '$(SBUILD_CHROOT)' was not found as a path or under $${HOME}/.cache/sbuild."; \
		echo "Run: make sbuild SBUILD_CHROOT_MODE=unshare, or pass SBUILD_CHROOT=/path/to/chroot."; \
		exit 1; \
	fi


.SUFFIXES: .pd_linux

pd_linux: sendVIMS.pd_linux selectors.h

selectors.h: parse-vims.pl vims.h
	perl parse-vims.pl vims.h > selectors.h

sendVIMS.pd_linux: selectors.h

PDINCLUDE ?= /usr/include/pd
PD_CFLAGS  = -DPD -fPIC -I$(PDINCLUDE)
STRIP ?= strip

.c.pd_linux:
	$(CC) $(PD_CFLAGS) $(OPTI_CFLAGS) $(DEBUG_CFLAGS) -DVERSION=\"$(VERSION)\" -o $*.o -c $*.c
	$(CC) -rdynamic  -shared -o $*.pd_linux $*.o -lm
	$(STRIP) --strip-unneeded $*.pd_linux
	rm $*.o

clean:
	rm -f *.pd_linux
	rm -f *~

INSTALL ?= install
prefix := $(patsubst %/,%,$(PREFIX))
PDLIBDIR ?= $(prefix)/lib/pd
PDEXTRADIR ?= $(PDLIBDIR)/extra
PDREFDIR ?= $(PDLIBDIR)/doc/5.reference

install: pd_linux
	$(INSTALL) -d "$(DESTDIR)$(PDEXTRADIR)" "$(DESTDIR)$(PDREFDIR)"
	$(INSTALL) -m 644 *.pd_* "$(DESTDIR)$(PDEXTRADIR)/"
	$(INSTALL) -m 644 help-*.pd "$(DESTDIR)$(PDREFDIR)/"

dist:	clean
	rm -rf ../$(PACKAGE)-$(VERSION)
	mkdir ../$(PACKAGE)-$(VERSION)
	cp -av * ../$(PACKAGE)-$(VERSION)
	rm -rf ../$(PACKAGE)-$(VERSION)/packaging-build
	cd .. && tar vczf $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION)
	rm -rf ../$(PACKAGE)-$(VERSION)

deb: check-sbuild check-flock
	@echo "==> Building Debian package with sbuild..."
	rm -rf $(PACKAGING_BUILD_DIR)/deb
	mkdir -p $(PACKAGING_BUILD_DIR)/deb
	@exec 9>"$(PACKAGING_DIST_LOCK)"; \
		$(FLOCK) 9; \
		$(MAKE) dist; \
		tar -C $(PACKAGING_BUILD_DIR)/deb -xzf ../$(PACKAGE)-$(VERSION).tar.gz; \
		tar -C $(PACKAGING_BUILD_DIR)/deb -czf $(PACKAGING_BUILD_DIR)/deb/$(DEB_SOURCE_PACKAGE)_$(VERSION).orig.tar.gz $(DEB_SOURCE_PACKAGE)-$(VERSION)
	cd $(PACKAGING_BUILD_DIR)/deb/$(DEB_SOURCE_PACKAGE)-$(VERSION) && dpkg-buildpackage -S -us -uc
	$(SBUILD) --arch=$(SBUILD_ARCH) --chroot-mode=$(SBUILD_CHROOT_MODE) --dist=$(SBUILD_DIST) --chroot=$(SBUILD_CHROOT) $(SBUILD_ARGS) \
		$(SBUILD_LOCAL_PACKAGE_ARG) \
		--build-dir=$(PACKAGING_BUILD_DIR)/deb \
		$(PACKAGING_BUILD_DIR)/deb/*.dsc
	@echo "==> Debian build complete. Artifacts in $(PACKAGING_BUILD_DIR)/deb/"

arch: check-flock
	@echo "==> Building Arch Linux package with makechrootpkg..."
	rm -rf $(PACKAGING_BUILD_DIR)/arch
	mkdir -p $(PACKAGING_BUILD_DIR)/arch
	@exec 9>"$(PACKAGING_DIST_LOCK)"; \
		$(FLOCK) 9; \
		$(MAKE) dist; \
		cp ../$(PACKAGE)-$(VERSION).tar.gz $(PACKAGING_BUILD_DIR)/arch/$(PACKAGE)-$(VERSION).tar.gz
	cp PKGBUILD $(PACKAGING_BUILD_DIR)/arch/
	cd $(PACKAGING_BUILD_DIR)/arch && sed -i 's/^pkgver=.*/pkgver=$(VERSION)/' PKGBUILD
	cd $(PACKAGING_BUILD_DIR)/arch && makechrootpkg $(ARCH_LOCAL_PACKAGE_ARGS) -r $(ARCH_CHROOT_DIR)
	@echo "==> Arch Linux build complete. Artifacts in $(PACKAGING_BUILD_DIR)/arch/"

rpm: check-flock
	@echo "==> Building RPM package with mock..."
	rm -rf $(PACKAGING_BUILD_DIR)/rpm
	mkdir -p $(PACKAGING_BUILD_DIR)/rpm/BUILD \
		$(PACKAGING_BUILD_DIR)/rpm/RPMS \
		$(PACKAGING_BUILD_DIR)/rpm/SOURCES \
		$(PACKAGING_BUILD_DIR)/rpm/SPECS \
		$(PACKAGING_BUILD_DIR)/rpm/SRPMS
	@exec 9>"$(PACKAGING_DIST_LOCK)"; \
		$(FLOCK) 9; \
		$(MAKE) dist; \
		cp ../$(PACKAGE)-$(VERSION).tar.gz \
			$(PACKAGING_BUILD_DIR)/rpm/SOURCES/$(DEB_SOURCE_PACKAGE)-$(VERSION).tar.gz
	cp $(RPM_SPEC_FILE) $(PACKAGING_BUILD_DIR)/rpm/SPECS/
	rpmbuild -bs \
		--define "_topdir $$(pwd)/$(PACKAGING_BUILD_DIR)/rpm" \
		$(PACKAGING_BUILD_DIR)/rpm/SPECS/$$(basename $(RPM_SPEC_FILE))
	@if test -n "$(strip $(MOCK_LOCAL_PACKAGES))"; then \
		$(MOCK) -r $(MOCK_CONFIG) $(MOCK_ARGS) --init; \
		$(MOCK) -r $(MOCK_CONFIG) $(MOCK_ARGS) --install $(MOCK_LOCAL_PACKAGES); \
	fi
	$(MOCK) -r $(MOCK_CONFIG) $(MOCK_ARGS) \
		$(if $(strip $(MOCK_LOCAL_PACKAGES)),--no-clean) \
		--resultdir=$(PACKAGING_BUILD_DIR)/rpm/RPMS \
		--rebuild $(PACKAGING_BUILD_DIR)/rpm/SRPMS/*.src.rpm
	@echo "==> RPM build complete. Artifacts in $(PACKAGING_BUILD_DIR)/rpm/"

clean-packaging:
	rm -rf $(PACKAGING_BUILD_DIR)
