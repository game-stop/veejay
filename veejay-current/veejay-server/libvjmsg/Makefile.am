# Makefile for  logging/messages 
INCLUDES = 	-I$(top_srcdir) -I$(includedir)
if !HAVE_MJPEGTOOLS
INCLUDES += -I$(top_srcdir)/mjpegtools
endif
INCLUDES += $(MJPEGTOOLS_CFLAGS)
INCLUDES += $(LIBUNWIND_CFLAGS)
 
AM_CLFAGS=$(OP_CFLAGS)
VJMSG_LIB_FILE = libvjmsg.la
noinst_LTLIBRARIES = $(VJMSG_LIB_FILE)
pkginclude_HEADERS= vj-msg.h
libvjmsg_la_SOURCES =	vj-common.c 

