# # Process this file with automake to produce Makefile.in.

EXTRA_DIST= ac.h ac_internal.h imgconvert.h img_internal.h img_x86_common.h

AM_CPPFLAGS = \
        $(PTHREAD_CFLAGS) \
        -I$(top_srcdir)

noinst_LTLIBRARIES = libac.la

libac_la_SOURCES = \
        accore.c \
        average.c \
        imgconvert.c \
        img_rgb_packed.c \
        img_yuv_mixed.c \
        img_yuv_packed.c \
        img_yuv_planar.c \
        img_yuv_rgb.c \
        memcpy.c \
        rescale.c

