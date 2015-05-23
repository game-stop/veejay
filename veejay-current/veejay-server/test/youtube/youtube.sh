#!/bin/bash
#
# simple bash script that encodes a youtube video to mjpeg avi file with audio for use in veejay
#
# example:
# Youtube url: https://www.youtube.com/watch?v=randomstring
#
# ./youtube randomstring videofile-name.avi
#
# this script assumes you have youtube-dl and ffmpeg installed
#
# youtube-dl
#
# To install it right away for all UNIX users (Linux, OS X, etc.), type:
#
#  sudo curl https://yt-dl.org/latest/youtube-dl -o /usr/local/bin/youtube-dl
#  sudo chmod a+x /usr/local/bin/youtube-dl
#  
# If you do not have curl, you can alternatively use a recent wget:
#
#    sudo wget https://yt-dl.org/downloads/latest/youtube-dl -O /usr/local/bin/youtube-dl
#    sudo chmod a+x /usr/local/bin/youtube-dl
#
#
# ffmpeg:
#    http://www.ffmpeg.org
#

function help {
  echo "Usage: $0 Youtube_Video_ID output.avi"
  exit 1
}

if [ -z "$1" ]
then
	help	
fi

tempfile="$$.avi"
tempaudio1="$$.unknown"
tempaudio2="$$.wav"

youtube-dl -fbest https://www.youtube.com/watch?v=$1 -o - |ffmpeg -i - -vcodec mjpeg -pix_fmt yuv422p -q:v 0 -an $tempfile

youtube-dl -fbest https://www.youtube.com/watch?v=$1 -o $tempaudio1

ffmpeg -i $tempaudio1 -acodec pcm_s16le -ar 48000 $tempaudio2

ffmpeg -i $tempfile -i $tempaudio2 -codec copy $2

rm $tempfile
rm $tempaudio1
rm $tempaudio2



