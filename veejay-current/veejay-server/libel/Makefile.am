# Makefile for veejay

AM_CFLAGS = $(VEEJAY_CFLAGS)

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty 

AM_CPPFLAGS += $(FFMPEG_CFLAGS) $(PIXBUF_CFLAGS) $(LIBQUICKTIME_CFLAGS) $(VEEJAYCORE_CFLAGS) $(NVJPEG_CFLAGS)

VJEL_LIB_FILE = libel.la
noinst_LTLIBRARIES = $(VJEL_LIB_FILE)
noinst_HEADERS = vj-ffmpeg-input.h vj-nvjpeg.h vj-nvjpeg-kernel.h
libel_la_SOURCES = vj-mmap.c elcache.c avilib.c lav_io.c anything.c vj-dv.c rawdv.c pixbuf.c vj-avcodec.c vj-ffmpeg-input.c vj-nvjpeg.c vj-el.c

if HAVE_NVJPEG_CUDA_KERNEL
NVJPEG_KERNEL_LO = vj-nvjpeg-kernel.lo
else
NVJPEG_KERNEL_LO =
endif

libel_la_LIBADD = $(PTHREAD_LIBS) $(NVJPEG_LIBS) $(NVJPEG_KERNEL_LO) $(NVJPEG_KERNEL_LIBS)
libel_la_DEPENDENCIES = $(NVJPEG_KERNEL_LO)

EXTRA_DIST = vj-nvjpeg-kernel.cu
CLEANFILES = vj-nvjpeg-kernel.lo vj-nvjpeg-kernel.o

if HAVE_NVJPEG_CUDA_KERNEL
vj-nvjpeg-kernel.lo: $(srcdir)/vj-nvjpeg-kernel.cu $(srcdir)/vj-nvjpeg-kernel.h
	$(LIBTOOL) --tag=CXX --mode=compile $(NVCC) $(NVJPEG_NVCCFLAGS) --forward-unknown-to-host-compiler -I$(top_srcdir) $(NVJPEG_CFLAGS) -c $(srcdir)/vj-nvjpeg-kernel.cu -o $@
endif
