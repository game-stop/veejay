INCLUDES = -I$(top_srcdir)/ccvt -I$(includedir)

CCVT_LIB_FILE = libccvt.la


noinst_LTLIBRARIES = $(CCVT_LIB_FILE)
libccvt_la_CFLAGS = -Wall -g -O3

libccvt_la_SOURCES = ccvt_mmx.S ccvt_misc.c ccvt_rgb2yuv.S
libccvt_la_LDFLAGS = -O3 -Wall -rdynamic
