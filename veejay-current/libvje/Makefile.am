# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg 

VJE_LIB_FILE = libvje.la

VJE_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(VJE_LIB_FILE)

libvje_la_SOURCES =	vj-effect.c vj-effman.c effects/common.c \
			effects/diff.c effects/fibdownscale.c effects/killchroma.c \
			effects/lumamagick.c\
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
			effects/crosspixel.c effects/cartonize.c effects/nervous.c effects/morphology.c effects/blob.c effects/boids.c effects/ghost.c


libvje_la_LDFLAGS = $(VJE_ALL_LIB_OPTS) \
			-L$(top_builddir)/libvjmem -lvjmem \
			-DDYNAMIC -O3 -Wall -rdynamic 

