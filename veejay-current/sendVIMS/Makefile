all: pd_linux

include Makefile.defs

VERSION = 0.1.5


.SUFFIXES: .pd_linux

pd_linux: sendVIMS.pd_linux selectors.h





PD_CFLAGS = -DPD

.c.pd_linux:
	$(CC) $(PD_CFLAGS) $(OPTI_CFLAGS) $(DEBUG_CFLAGS) -DVERSION=\"$(VERSION)\" -o $*.o -c $*.c
	$(CC) -rdynamic  -shared -o $*.pd_linux $*.o -lm
	strip --strip-unneeded $*.pd_linux
	rm $*.o

clean:
	rm -f *.pd_linux
	rm -f *~

install:
	install -m 644 *.pd_* $(PREFIX)/lib/pd/extra
	install -m 644 help-*.pd $(PREFIX)/lib/pd/doc/5.reference/

dist:	clean
	rm -rf ../sendVIMS-$(VERSION)
	mkdir ../sendVIMS-$(VERSION)
	cp -av * ../sendVIMS-$(VERSION)
	cd .. && tar vczf sendVIMS-$(VERSION).tar.gz sendVIMS-$(VERSION)
	rm -rf ../sendVIMS-$(VERSION)
