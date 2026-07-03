all: pd_linux

include Makefile.defs

VERSION = 0.1.6


.SUFFIXES: .pd_linux

pd_linux: sendVIMS.pd_linux selectors.h

selectors.h: parse-vims.pl vims.h
	perl parse-vims.pl vims.h > selectors.h

sendVIMS.pd_linux: selectors.h

PD_CFLAGS = -DPD

.c.pd_linux:
	$(CC) $(PD_CFLAGS) $(OPTI_CFLAGS) $(DEBUG_CFLAGS) -DVERSION=\"$(VERSION)\" -o $*.o -c $*.c
	$(CC) -rdynamic  -shared -o $*.pd_linux $*.o -lm
	strip --strip-unneeded $*.pd_linux
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
	rm -rf ../sendVIMS-$(VERSION)
	mkdir ../sendVIMS-$(VERSION)
	cp -av * ../sendVIMS-$(VERSION)
	cd .. && tar vczf sendVIMS-$(VERSION).tar.gz sendVIMS-$(VERSION)
	rm -rf ../sendVIMS-$(VERSION)
