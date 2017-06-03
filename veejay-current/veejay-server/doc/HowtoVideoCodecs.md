## Video files
### A video file consist out of two SEPARATE elements
* container
* codec 

The container holds the digitally encoded data and the codec is capable of decoding/encoding this digitally encoded data.

Veejay supports the AVI and the Quicktime container, with the following codecs:

### Quicktime
* mpjeg,mjpa,jpeg,dmb1,mpng,png
* dvsd, dv, dvcp, dvhd 

### AVI
* mjpeg, mjpa,jpeg,jfif,dmb1,mpng,png
* dvsd, dv, dvcp, dvhd
* i420, i422, yv16, hfyu 

### Raw DV
* PAL / NTSC dvsd 

Veejay **can only deal** with video files that consists entirely out of whole images, **only I-frames**. 

## Which codec to use

MotionJPEG (mjpeg) is the veejay codec of choice for most applications, it gives you a good tradeof between compression, quality and compatibility. If you want speed, use AVI yv16 or i420 while recording to new samples.

### Tools that support MJPEG:
* http://cvs.cinelerra.org/Cinelerra
* http://www.kinodv.org/Kino
* http://ronald.bitfreak.net/lvs/Linux video studio
* http://mjpeg.sourceforge.net/mjpegtools
* http://www.mplayerhq.huMplayer, and mencoder
* http://lives.sf.net Lives 

## Wich resolutions to use
Veejay can do:
* high definition 
* pal: 720x576 
* ntsc: 720x480
* 1/4 pal: 360x288 
* 1/4 ntsc: 360x240 

If you load multiple video files on the commandline, make sure that all files have the same resolution and audio properties.

## How to convert
### You can use FFMpeg

̀`$ ffmpeg -i input-file -vcodec mjpeg -pix_fmt yuvj422p -q:v 0 output-file.avi`

Optionally add PCM 16bit audio, 44.1/48.0 Khz, 2 channels, 8 bits per channel

`$ ffmpeg -i input-file -vcodec mjpeg -s 128x128 -pix_fmt yuvj422p -acodec pcm_s16le -ar 44100 -ac 2 output-file.avi`

### Or, you can use mplayer
`$ mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -o <outputfile> <inputfile>`

### To scale on the fly, use
`$ mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -vf scale=352:288 -o <outputfile> <inputfile>`

consult mplayer documentation about other options, such as cropping and filtering out blocks in video.

### a quick hint for bulk encoding a bunch of capture.dv files
̀```$ for i in `ls *dv`;do mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -o `echo $i | sed s/.dv/.avi/` $i; done;```



## What is Dummy mode ?

Dummy mode opens up a 'color stream' to start veejay without a video file.

If you use a video file, veejay will take that file's properties as default settings for the whole session.

