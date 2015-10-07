# Makefile for veejay
EXTRA_DIST= qrwrapper.h
MAINTAINERCLEANFILES=Makefile.in
AM_CFLAGS = $(OP_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) 
QRWRAP_LIB_FILE = libqrwrap.la
noinst_LTLIBRARIES = $(QRWRAP_LIB_FILE)
libqrwrap_la_SOURCES = qrwrap.c 

