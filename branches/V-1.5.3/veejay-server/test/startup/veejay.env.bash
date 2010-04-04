#!/bin/bash
# use SDL hardware acceleration

if [ -z $@ ]
then
	veejay
	exit 
fi

export SDL_VIDEO_HWACCEL=1

# setup video window on secundary monitor (twinview desktop)
# desktop=2526x1024, second screen starts at 1600, size is 1024x768
export VEEJAY_SCREEN_GEOMETRY=2624x1024+1600x0
export VEEJAY_SCREEN_SIZE=1024x768

# highest possible quest
export VEEJAY_PERFORMANCE=quality

# start veejay in verbose, output video in 1024x768
# expand parameters given to script
veejay -v -w1024 -h768 $@
