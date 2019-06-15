# Makefile for veejay
AM_CFLAGS=$(SUBSAMPLE_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
	      -I$(top_srcdir)/thirdparty 

SUBSAMPLE_LIB_FILE = libsubsample.la
noinst_LTLIBRARIES = $(SUBSAMPLE_LIB_FILE)
libsubsample_la_SOURCES = subsample.c 
