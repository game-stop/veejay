# Process this file with Automake to produce Makefile.in

INCLUDES = -I$(top_srcdir)


noinst_LIBRARIES = libmotion.a libmjpegutils.a
#lib_LIBRARIES = libmjpegutils.a

libmotion_MMXSSE =  mblock_sumsq_mmx.s mblock_bsumsq_mmx.s mblock_bsad_mmx.s \
                    mblock_sad_mmxe.s mblock_sad_mmx.s  \
                    mblock_sub44_sads_x86.c attributes.h

libmjpegutils_a_SOURCES = mjpeg_logging.c cpu_accel.c \
                          mjpeg_logging.h mjpeg_types.h cpu_accel.h \
                          yuv4mpeg.c yuv4mpeg_ratio.c \
                          yuv4mpeg.h yuv4mpeg_intern.h \
                          mpegconsts.c mpegconsts.h \
                          mpegtimecode.c mpegtimecode.h

#include_HEADERS = yuv4mpeg.h mpegconsts.h mjpeg_logging.h mjpeg_types.h mpegtimecode.h

if HAVE_ASM_NASM
if HAVE_ASM_MMX
libmotion_SIMD = $(libmotion_MMXSSE)
else
mpeg2enc_SIMD = 
endif
else
mpeg2enc_SIMD = 
endif

libmotion_a_SOURCES = motionsearch.c $(libmotion_SIMD) mmx.h motionsearch.h fastintfns.h

EXTRA_DIST = videodev_mjpeg.h format_codes.h mblock_sub44_sads_x86_h.c mblock_sub44_sads_x86.h

MAINTAINERCLEANFILES = Makefile.in

%.o: %.s ; 	$(AS) $(ASFLAGS) -o $@ $<

