# Makefile for  logging/messages 

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/thirdparty/mjpegtools
AM_CPPFLAGS += $(LIBUNWIND_CFLAGS)

VJMSG_LIB_FILE = libvjmsg.la
noinst_LTLIBRARIES = $(VJMSG_LIB_FILE)

pkginclude_HEADERS= vj-msg.h
libvjmsg_la_SOURCES = vj-common.c 

