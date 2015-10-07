# Makefile for  logging/messages 

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir)
if !HAVE_MJPEGTOOLS
AM_CPPFLAGS += -I$(top_srcdir)/thirdparty/mjpegtools
endif
AM_CPPFLAGS += $(MJPEGTOOLS_CFLAGS)
AM_CPPFLAGS += $(LIBUNWIND_CFLAGS)

VJMSG_LIB_FILE = libvjmsg.la
noinst_LTLIBRARIES = $(VJMSG_LIB_FILE)

pkginclude_HEADERS= vj-msg.h
libvjmsg_la_SOURCES =	vj-common.c 

