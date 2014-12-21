# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg
 
VJLZO_LIB_FILE = libvjlzo.la
noinst_LTLIBRARIES = $(VJLZO_LIB_FILE)
libvjlzo_la_SOURCES = minilzo.c lzo.c 
EXTRA_DIST= lzoconf.h lzodefs.h lzo.h minilzo.h
