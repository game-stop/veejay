# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg 
 
VJLZO_LIB_FILE = liblzo.la
noinst_LTLIBRARIES = $(VJLZO_LIB_FILE)
liblzo_la_SOURCES = minilzo.c lzo.c 
