MAINTAINERCLEANFILES = Makefile.in
	
INCLUDES = 	-I$(top_srcdir) \
		-I$(includedir) \
		-I${LIBLO_CFLAGS}

sayOSC_SOURCES	= veejay-talk.c
sayOSC_LDADD   = ${LIBLO_LIBS}

YUV2RAWDV_BIN = sayOSC

bin_PROGRAMS = \
	$(YUV2RAWDV_BIN)

	
