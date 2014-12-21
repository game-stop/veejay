MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	$(GLIB_CFLAGS) -I$(top_srcdir)/libvevo -I$(includedir) -I$(top_srcdir)/libhash -I$(top_srcdir)/libvjmem -I$(top_srcdir)/libvjmsg 

VEVO_LIB_FILE = libvevo.la
pkginclude_HEADERS = libvevo.h lowlevel.h pool.h vevo.h
noinst_LTLIBRARIES = $(VEVO_LIB_FILE)

libvevo_la_SOURCES = pool.c vevo.c  
