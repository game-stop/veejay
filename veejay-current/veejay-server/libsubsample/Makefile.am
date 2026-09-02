# Makefile for veejay
AM_CFLAGS = $(VEEJAY_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
	      -I$(top_srcdir)/thirdparty 

SUBSAMPLE_LIB_FILE = libsubsample.la
noinst_LTLIBRARIES = $(SUBSAMPLE_LIB_FILE)
EXTRA_DIST = *.h
libsubsample_la_SOURCES = subsample.c subsample-neon.c subsample-altivec.c \
	subsample-esp32.c

check_PROGRAMS = subsample-esp32-test
subsample_esp32_test_CPPFLAGS = $(AM_CPPFLAGS) -DHAVE_ESP32=1
subsample_esp32_test_SOURCES = subsample-backend-test.c subsample-esp32.c

if HAVE_ALTIVEC
check_PROGRAMS += subsample-altivec-test
subsample_altivec_test_CPPFLAGS = $(AM_CPPFLAGS) -DHAVE_ASM_ALTIVEC=1
subsample_altivec_test_SOURCES = subsample-backend-test.c subsample-altivec.c
endif

check-local: subsample-esp32-test
	$(builddir)/subsample-esp32-test$(EXEEXT)
if HAVE_ALTIVEC
	$(builddir)/subsample-altivec-test$(EXEEXT)
endif
