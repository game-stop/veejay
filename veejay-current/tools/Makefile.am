MAINTAINERCLEANFILES = Makefile.in
	
INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/utils
UTILS = $(top_builddir)/utils/libmjpegutils.a

yuv2rawdv_SOURCES = yuv2rawdv.c 
yuv2rawdv_LDFLAGS = $(DV_LIBS) $(LAV_ALL_LIB_OPTS)
yuv2rawdv_LDADD = $(UTILS) @LIBGETOPT_LIB@
rawdv2yuv_SOURCES = rawdv2yuv.c
rawdv2yuv_LDFLAGS = $(DV_LIBS) $(LAV_ALL_LIB_OPTS)
rawdv2yuv_LDADD = $(UTILS) @LIBGETOPT_LIB@
any2yuv_SOURCES = any2yuv.c
any2yuv_LDFLAGS = $(LAV_ALL_LIB_OPTS)
any2yuv_LDADD =  $(UTILS) @LIBGETOPT_LIB@

sayVIMS_SOURCES = sayVIMS.c

YUV2RAWDV_BIN = yuv2rawdv rawdv2yuv any2yuv sayVIMS

bin_PROGRAMS = \
	$(YUV2RAWDV_BIN)

	
