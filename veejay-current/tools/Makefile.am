MAINTAINERCLEANFILES = Makefile.in
	
INCLUDES = 	-I$(top_srcdir) \
		-I$(includedir) \
		-I$(top_scrdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		$(MJPEGTOOLS_CFLAGS) $(GLIB_CFLAGS) 

VJLIBS = $(top_builddir)/libvjnet/libvjnet.la \
	 $(top_builddir)/libvjmsg/libvjmsg.la \
	 $(top_builddir)/libvjmem/libvjmem.la

yuv2rawdv_SOURCES = yuv2rawdv.c 
yuv2rawdv_LDFLAGS = $(LIBDV_LIBS) $(LAV_ALL_LIB_OPTS)
yuv2rawdv_LDADD = $(MJPEGTOOLS_LIBS) @LIBGETOPT_LIB@
rawdv2yuv_SOURCES = rawdv2yuv.c
rawdv2yuv_LDFLAGS = $(LIBDV_LIBS) $(LAV_ALL_LIB_OPTS)
rawdv2yuv_LDADD = $(MJPEGTOOLS_LIBS) @LIBGETOPT_LIB@
any2yuv_SOURCES = any2yuv.c
any2yuv_LDFLAGS = $(LAV_ALL_LIB_OPTS)
any2yuv_LDADD =  $(MJPEGTOOLS_LIBS) @LIBGETOPT_LIB@
sayVIMS_SOURCES = sayVIMS.c
sayVIMS_LDFLAGS = $(LAV_ALL_LIB_OPTS)
sayVIMS_LDADD = $(VJLIBS) @LIBGETOPT_LIB@
if USE_GDK_PIXBUF
tracktest_SOURCES = sequenc.c 
tracktest_LDFLAGS = $(LAV_ALL_LIB_OPTS) $(GLIB_LIBS) -lgthread-2.0
tracktest_LDADD   = $(VJLIBS) @LIBGETOPT_LIB@ 
endif

YUV2RAWDV_BIN = yuv2rawdv rawdv2yuv any2yuv sayVIMS tracktest

bin_PROGRAMS = \
	$(YUV2RAWDV_BIN)

	
