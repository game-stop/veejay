# Makefile for libpostproc

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) \
		-I$(includedir) \
		-I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libpostproc \
		$(CFLAGS) $(ALTIVEC_CFLAGS)

POSTPROC_LIB_FILE = libpostproc.la
noinst_LTLIBRARIES = $(POSTPROC_LIB_FILE)
libpostproc_la_SOURCES = swscale.c rgb2rgb.c yuv2rgb.c yuv2rgb_altivec.c
