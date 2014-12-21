# Makefile for veejay
CFLAGS=
AM_CFLAGS=$(SUBSAMPLE_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg  
SUBSAMPLE_LIB_FILE = libsubsample.la
pkginclude_HEADERS=subsample.h
EXTRA_DIST = mmx.h subsample.h mmx_macros.h 
noinst_LTLIBRARIES = $(SUBSAMPLE_LIB_FILE)
libsubsample_la_SOURCES = subsample.c 
