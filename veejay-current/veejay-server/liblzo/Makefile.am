# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg
CFLAGS="$CFLAGS -DLZO_HAVE_CONFIG_H"
 
VJLZO_LIB_FILE = libminilzo.la
noinst_LTLIBRARIES = $(VJLZO_LIB_FILE)
libminilzo_la_SOURCES = minilzo.c lzo.c 
EXTRA_DIST= lzoconf.h lzodefs.h lzo.h minilzo.h
