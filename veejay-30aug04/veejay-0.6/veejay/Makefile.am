# Makefile for veejay

INCLUDES =  -I$(top_srcdir) -I$(includedir) \
	-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
        -I /usr/X11R6/include -I$(top_srcdir)/libOMC -I$(top_srcdir)/utils \
	-I$(top_srcdir)/ffmpeg/libavcodec -I$(top_srcdir)/ffmpeg/libavcodec \
	$(AVIPLAY_CFLAGS) ${GTK_CFLAGS} ${DV_FLAGS} ${X_CFLAGS} ${XML_CFLAGS} \
	$(DIRECTFB_CFLAGS) ${SDL_CFLAGS} ${JPEG_CFLAGS} ${PTHREAD_CFLAGS}  

#${IMLIB2_CFLAGS}


# *********************************************************************


VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

noinst_LTLIBRARIES = liblavjpeg.la 



#libveejayinclude_HEADERS = avilib.h colorspace.h editlist.h fastmemcpy.h \
#			hash.h jpegutils.h cpu_accel.h lav_common.h lav_io.h sampleadm.h \
#			libveejay.h vj-lib.h samplerecord.h subsample.h v4lutils.h \
#			vj-OSC.h vj-audio.h vj-client.h vj-common.h vj-dfb.h \
#			vj-dv.h vj-effect.h vj-effman.h vj-event.h vj-ffmpeg.h vj-frameinfo.h \
#			vj-global.h vj-keyframe.h vj-lib.h vj-motion.h vj-perform.h vj-rgb.h \
#			vj-sdl.h vj-server.h vj-tag.h vj-v4lvideo.h vj-vloopback.h \
#			vj-yuv4mpeg.h                  


VEEJAY_ALL_LIB_OPTS = \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

liblavjpeg_la_SOURCES = jpegutils.c 
liblavjpeg_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS) $(JPEG_LIBS)

libveejay_la_SOURCES =  vj-common.c cpu_accel.c memcpy.c subsample.c hash.c colorspace.c avilib.c vj-shm.c lav_io.c \
			v4lutils.c vj-server.c vj-client.c vj-osc.c vj-event.c vj-pjack.c vj-perform.c vj-frameinfo.c \
			vj-keyframe.c vj-effect.c vj-effman.c vj-motion.c vj-sdl.c vj-dfb.c sampleadm.c samplerecord.c \
			vj-vloopback.c vj-yuv4mpeg.c vj-rgb.c vj-v4lvideo.c vj-dv.c vj-ffmpeg.c vj-tag.c vj-audio.c editlist.c \
			effects/common.c effects/diff.c effects/fibdownscale.c effects/killchroma.c effects/lumamagick.c\
		        effects/mirrors.c effects/negation.c effects/overclock.c effects/dissolve.c effects/opacity.c effects/posterize.c \
			effects/dither.c effects/dices.c effects/emboss.c effects/flip.c \
			effects/revtv.c effects/softblur.c effects/zoom.c effects/split.c effects/widthmirror.c \
			effects/frameborder.c effects/dummy.c effects/rawman.c effects/rawval.c \
			effects/smuck.c effects/transform.c effects/borders.c \
			effects/rgbkey.c effects/solarize.c effects/mirrors2.c \
			effects/whiteframe.c effects/deinterlace.c effects/lumakey.c \
			effects/coloradjust.c effects/gamma.c effects/reflection.c effects/rotozoom.c \
			transitions/transblend.c transitions/slidingdoor.c effects/color.c \
			transitions/transline.c transitions/transop.c transitions/transcarot.c \
			transitions/fadecolor.c effects/lumablend.c effects/chromamagick.c \
			transitions/fadecolorrgb.c effects/magicoverlays.c  effects/binaryoverlays.c effects/pencilsketch.c effects/diffimg.c \
			effects/swirl.c effects/chromium.c effects/uvcorrect.c effects/chromapalette.c effects/radialblur.c effects/fisheye.c effects/raster.c effects/smear.c effects/magicmirror.c effects/lumamask.c effects/pixelate.c effects/colorshift.c effects/scratcher.c effects/opacitythreshold.c \
			effects/opacityadv.c effects/rgbkeysmooth.c effects/mask.c effects/distort.c \
			transitions/wipe.c effects/magicscratcher.c effects/chromascratcher.c \
			effects/tracer.c effects/mtracer.c effects/dupmagic.c effects/keyselect.c \
			effects/greyselect.c effects/bwselect.c effects/complexsaturate.c \
			effects/complexthreshold.c effects/complexinvert.c effects/complexsync.c \
			effects/isolate.c transitions/vbar.c transitions/3bar.c effects/enhancemask.c effects/noiseadd.c \
			effects/contrast.c effects/motionblur.c effects/sinoids.c effects/average.c \
		        effects/ripple.c effects/water.c effects/noisepencil.c effects/bathroom.c effects/slice.c \
			effects/crosspixel.c liblavplayvj.c


libveejay_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS) -L$(top_builddir)/ffmpeg/libavcodec/ -lavcodec \
			-L$(top_builddir)/ccvt/ -lccvt \
			-L$(top_builddir)/libOMC/ -lOMC \
			-L$(top_builddir)/utils/ -lmjpegutils \
			-L$(top_builddir)/bio2jack/ -lbio2jack \
			${SDL_LIBS} ${DIRECTFB_LIBS} ${PTHREAD_LIBS} ${X_LIBS} \
			${XML_LIBS} ${JPEG_LIBS} \
			${DV_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)  

veejay_headers = vj-lib.h vj-event.h libveejay.h vj-common.h

veejay_SOURCES = veejay.c ${veejay_headers} 
veejay_LDADD =  libveejay.la liblavjpeg.la @LIBGETOPT_LIB@ @JACK_LIBS@ 
