# Makefile for libvjnet
AM_CFLAGS=$(OP_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/libyuv \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg 

AM_CPPFLAGS += $(FFMPEG_CFLAGS)

VJNET_LIB_FILE = libvjnet.la
EXTRA_DIST = vj-client.h bswap.h cmd.h mcastreceiver.h mcastsender.h packet.h vj-server.h
pkginclude_HEADERS = vj-client.h vj-server.h
noinst_LTLIBRARIES = $(VJNET_LIB_FILE)
libvjnet_la_SOURCES =	packet.c mcastsender.c mcastreceiver.c cmd.c vj-server.c vj-client.c
