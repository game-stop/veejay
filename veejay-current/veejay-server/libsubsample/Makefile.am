# Makefile for veejay
CFLAGS=
AM_CFLAGS=$(SUBSAMPLE_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libyuv 
SUBSAMPLE_LIB_FILE = libsubsample.la
pkginclude_HEADERS=subsample.h
EXTRA_DIST = subsample.h  
noinst_LTLIBRARIES = $(SUBSAMPLE_LIB_FILE)
libsubsample_la_SOURCES = subsample.c 
