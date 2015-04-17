# Makefile for veejay

MAINTAINERCLEANFILES=Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg $(FFMPEG_CFLAGS) $(GLIB_CFLAGS)   
CFLAGS =
AM_CFLAGS = $(OPTCFLAGS) $(FREEFRAME_CFLAGS)

pkginclude_HEADERS=vje.h

EXTRA_DIST = internal.h vje.h
EXTRA_DIST += effects/autoeq.h effects/crosspixel.h effects/mask.h effects/rawval.h \
		      effects/average.h effects/cutstop.h effects/maskstop.h effects/reflection.h \
		      effects/baltantv.h effects/deinterlace.h effects/mirrors2.h effects/revtv.h \
		      effects/bathroom.h effects/dices.h effects/mirrors.h effects/rgbchannel.h \
		      effects/bgsubtract.h effects/diff.h effects/morphology.h effects/rgbkey.h \
		      effects/binaryoverlays.h effects/diffimg.h effects/motionblur.h effects/rgbkeysmooth.h \
		      effects/blob.h effects/diffmap.h effects/motionmap.h effects/ripple.h \
		      effects/boids.h effects/dissolve.h effects/mtracer.h effects/rippletv.h \
		      effects/borders.h effects/distort.h effects/negatechannel.h effects/rotozoom.h \
		      effects/bwselect.h effects/dither.h effects/negation.h effects/scratcher.h \
		      effects/cartonize.h effects/dummy.h effects/neighbours2.h effects/sinoids.h \
		      effects/chameleonblend.h effects/dupmagic.h effects/neighbours3.h effects/slice.h \
		      effects/chameleon.h effects/emboss.h effects/neighbours4.h effects/slicer.h \
		      effects/chromamagick.h effects/enhancemask.h effects/neighbours5.h effects/smear.h \
		      effects/chromapalette.h effects/fibdownscale.h effects/neighbours.h effects/smuck.h \
		      effects/chromascratcher.h effects/fisheye.h effects/nervous.h effects/softblur.h \
		      effects/chromium.h effects/flare.h effects/noiseadd.h effects/solarize.h \
		      effects/colflash.h effects/flip.h effects/noisepencil.h effects/split.h \
		      effects/colmorphology.h effects/frameborder.h effects/opacityadv.h effects/swirl.h \
		      effects/coloradjust.h effects/gamma.h effects/opacity.h effects/iris.h \
		      effects/color.h effects/ghost.h effects/opacitythreshold.h effects/threshold.h \
		      effects/colorhis.h effects/greyselect.h effects/overclock.h effects/timedistort.h \
		      effects/colormap.h effects/isolate.h effects/pencilsketch.h effects/tracer.h \
		      effects/colorshift.h effects/keyselect.h effects/photoplay.h effects/transform.h \
		      effects/common.h effects/killchroma.h effects/picinpic.h effects/tripplicity.h \
		      effects/complexinvert.h effects/lumablend.h effects/pixelate.h effects/uvcorrect.h \
		      effects/complexsaturate.h effects/lumakey.h effects/posterize.h effects/videoplay.h \
		      effects/complexsync.h effects/lumamagick.h effects/radcor.h effects/videowall.h \
		      effects/complexthreshold.h effects/lumamask.h effects/radialblur.h effects/waterrippletv.h \
		      effects/constantblend.h effects/magicmirror.h effects/radioactive.h effects/whiteframe.h \
		      effects/contourextract.h effects/magicoverlays.h effects/raster.h effects/widthmirror.h \
		      effects/contrast.h effects/magicscratcher.h effects/rawman.h effects/zoom.h effects/iris.h effects/cali.h effects/median.h effects/average-blend.h

EXTRA_DIST += specs/FreeFrame.h specs/frei0r.h

EXTRA_DIST += transitions/3bar.h transitions/fadecolorrgb.h transitions/transblend.h transitions/transline.h \
		      transitions/vbar.h transitions/fadecolor.h transitions/slidingdoor.h transitions/transcarot.h \
		      transitions/transop.h transitions/wipe.h

EXTRA_DIST += ctmf/ctmf.h

VJE_LIB_FILE = libvje.la
noinst_LTLIBRARIES = $(VJE_LIB_FILE)

libvje_la_SOURCES =	vj-effect.c vj-effman.c effects/common.c \
			ctmf/ctmf.c \
			effects/diff.c effects/fibdownscale.c effects/killchroma.c \
			effects/lumamagick.c effects/autoeq.c effects/colorhis.c \
		        effects/mirrors.c effects/colormap.c effects/negatechannel.c effects/negation.c \
			effects/overclock.c effects/dissolve.c effects/opacity.c effects/iris.c effects/posterize.c \
			effects/dither.c effects/dices.c effects/emboss.c effects/flip.c \
			effects/revtv.c effects/softblur.c effects/zoom.c effects/split.c effects/widthmirror.c \
			effects/frameborder.c effects/dummy.c effects/rawman.c effects/rawval.c \
			effects/smuck.c effects/transform.c effects/borders.c effects/colflash.c \
			effects/rgbkey.c effects/solarize.c effects/mirrors2.c effects/timedistort.c \
			effects/whiteframe.c effects/deinterlace.c effects/lumakey.c effects/chameleon.c \
			effects/coloradjust.c effects/gamma.c effects/reflection.c effects/rotozoom.c \
			transitions/transblend.c transitions/slidingdoor.c effects/color.c \
			transitions/transline.c transitions/transop.c transitions/transcarot.c \
			transitions/fadecolor.c effects/lumablend.c effects/chromamagick.c \
			transitions/fadecolorrgb.c effects/magicoverlays.c  effects/binaryoverlays.c \
			effects/pencilsketch.c effects/chameleonblend.c effects/diffimg.c \
			effects/swirl.c effects/chromium.c effects/uvcorrect.c effects/chromapalette.c \
			effects/radialblur.c effects/fisheye.c effects/raster.c effects/smear.c effects/magicmirror.c \
			effects/lumamask.c effects/pixelate.c effects/colorshift.c effects/scratcher.c effects/opacitythreshold.c \
			effects/opacityadv.c effects/rgbkeysmooth.c effects/mask.c effects/distort.c effects/motionmap.c \
			transitions/wipe.c effects/magicscratcher.c effects/chromascratcher.c \
			effects/tracer.c effects/mtracer.c effects/dupmagic.c effects/keyselect.c \
			effects/greyselect.c effects/bwselect.c effects/complexsaturate.c \
			effects/complexthreshold.c effects/complexinvert.c effects/complexsync.c \
			effects/isolate.c transitions/vbar.c transitions/3bar.c effects/enhancemask.c effects/noiseadd.c \
			effects/contrast.c effects/motionblur.c effects/sinoids.c effects/average.c effects/slicer.c \
		        effects/ripple.c effects/waterrippletv.c effects/water.c effects/noisepencil.c effects/bathroom.c effects/slice.c \
			effects/crosspixel.c effects/diffmap.c effects/threshold.c effects/morphology.c effects/colmorphology.c \
			effects/ghost.c effects/blob.c effects/radcor.c effects/boids.c effects/nervous.c \
			effects/cartonize.c effects/tripplicity.c effects/neighbours.c effects/neighbours2.c\
			effects/neighbours3.c effects/neighbours4.c effects/neighbours5.c effects/cutstop.c\
			effects/contourextract.c effects/maskstop.c effects/photoplay.c effects/videoplay.c effects/rgbchannel.c \
			effects/videowall.c effects/flare.c effects/radioactive.c effects/baltantv.c effects/constantblend.c effects/picinpic.c effects/bgsubtract.c effects/cali.c effects/median.c effects/average-blend.c


