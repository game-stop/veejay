# Makefile for veejay

MAINTAINERCLEANFILES=Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg

AM_CPPFLAGS += $(FFMPEG_CFLAGS) $(GLIB_CFLAGS)

AM_CFLAGS = $(VJE_CFLAGS) 

VJE_LIB_FILE = libvje.la
noinst_LTLIBRARIES = $(VJE_LIB_FILE)

libvje_la_SOURCES =	vj-effect.c vj-effman.c effects/common.c \
			ctmf/ctmf.c \
			effects/diff.c effects/fibdownscale.c effects/killchroma.c \
			effects/lumamagick.c effects/autoeq.c effects/colorhis.c \
		    effects/mirrors.c effects/colormap.c effects/negatechannel.c effects/negation.c \
			effects/overclock.c effects/dissolve.c effects/opacity.c effects/iris.c effects/posterize.c effects/posterize2.c \
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
			effects/videowall.c effects/flare.c effects/radioactive.c effects/baltantv.c effects/constantblend.c effects/picinpic.c effects/bgsubtract.c effects/cali.c effects/median.c effects/average-blend.c effects/perspective.c \
			effects/toalpha.c effects/mixtoalpha.c effects/alpha2img.c effects/alphafill.c effects/alphaflatten.c \
			effects/magicalphaoverlays.c effects/travelmatte.c effects/feathermask.c effects/alphaselect.c \
		   effects/alphaselect2.c effects/alphablend.c effects/porterduff.c effects/alphanegate.c effects/lumakeyalpha.c \
	   		effects/chromamagickalpha.c	effects/magicoverlaysalpha.c effects/gaussblur.c effects/levelcorrection.c \
		   effects/masktransition.c effects/alphadampen.c effects/passthrough.c effects/alphatransition.c effects/randnoise.c \
		   effects/bgsubtractgauss.c effects/bwotsu.c effects/meanfilter.c effects/bgpush.c effects/pixelsort.c effects/pixelsortalpha.c \
		   effects/stretch.c effects/bloom.c effects/gammacompr.c effects/squares.c effects/halftone.c effects/dotillism.c


