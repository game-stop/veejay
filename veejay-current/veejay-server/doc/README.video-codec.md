
     __ __  ____  ___      ___   ___      __   ___   ___      ___     __ 
    |  |  ||    ||   \    /  _] /   \    /  ] /   \ |   \    /  _]   /  ]
    |  |  | |  | |    \  /  [_ |     |  /  / |     ||    \  /  [_   /  / 
    |  |  | |  | |  D  ||    _]|  O  | /  /  |  O  ||  D  ||    _] /  /  
    |  :  | |  | |     ||   [_ |     |/   \_ |     ||     ||   [_ /   \_ 
     \   /  |  | |     ||     ||     |\     ||     ||     ||     |\     |
      \_/  |____||_____||_____| \___/  \____| \___/ |_____||_____| \____|




Video files
=========================
Basic knowledge
--------
A video file consist out of two SEPARATE elements:
- container
- codec

The container holds the digitally encoded data and the codec is capable of decoding/encoding this digitally encoded data.

### Veejay's file format support

Veejay supports the AVI and the Quicktime container, with the following codecs:

- **Quicktime**
    - mpjeg,mjpa,jpeg,dmb1,mpng,png
    - dvsd, dv, dvcp, dvhd
- **AVI**
    - mjpeg, mjpa,jpeg,jfif,dmb1,mpng,png
    - dvsd, dv, dvcp, dvhd
    - i420, i422, yv16, hfyu
- **Raw DV**
    - PAL / NTSC dvsd

**Important:** Veejay **can only deal** with video files that consists entirely out of whole images, **only I-frames**.

You can also load mime type `image/jpeg` images ( .jpg, .jpeg, .JPG, .JPEG)

so... Which codec to use ?
-------------
[MotionJPEG](https://en.wikipedia.org/wiki/Motion_JPEG) (mjpeg) is the veejay codec of choice for most applications, it gives you a good tradeof between compression, quality and compatibility. M-JPEG is an intraframe-only compression, each video frame is coded separately as a JPEG image, giving advantage of rapidly changing motion in the video stream.

If you ever want more speed, use AVI `yv16` or `i420` before recording to new samples :

* using console client and **sayVIMS** (`selector 302` _Set codec to use for recording_  (use 'x' to see list))
* from gui client **reloaded** [Sample/Stream panel] ---> [Recording to Disk panel] ---> [Codec drop down list].

### Floss tools that support MJPEG:
* [Cinelerra](http://cinelerra.org/) - Video editing and compositing software
* [Kdenlive](https://kdenlive.org/fr/) - Video editing software
* [Shotcut](https://shotcut.org/) - Video editing software
* [MJPEG Tools](http://mjpeg.sourceforge.net/) - Set of tools for videos recording and playback
* [Mplayer, and mencoder](http://www.mplayerhq.hu/) - Movie player which runs on many systems
* [FFmpeg](https://ffmpeg.org) - Vast suite of libraries and programs for handling video, audio, and other multimedia files and streams
* [Libav](https://libav.org) - FFmpeg fork, has a second choice
* [VLC ](https://www.videolan.org/) - Media player, streaming media server and more...
* [Lives](http://lives-video.com/) - VJ and Video Editing system
* [Kino](http://www.kinodv.org/Kino) (_not been actively maintained since 2009_)
* [Linux Video Studio](http://ronald.bitfreak.net/lvs/index.shtml) (_not actively maintained_)


Which resolutions to use ?
------------
Veejay can do:

* high resolutions aka HDTV
* pal 16/9 : 1024x576
* pal: 720x576
* ntsc: 720x480
* 1/4 pal: 360x288
* 1/4 ntsc: 360x240

In fact pretty much any resolution divisible by 8, which is a limitation for some video internals (encoders and effects). Finally it is an everyone finding balance between preciseness of graphics, complexity of effect's chain and necessary resources for a realtime accuracy.

If you load multiple video files **on the command line**, make sure that all files have the same resolution and audio properties.

How to convert ?
-------------
### You can use FFMpeg

```shell
$ ffmpeg -i input-file -c:v mjpeg -pix_fmt yuvj422p -q:v 1 -an output-file.avi
```
Your input video `input-file` will be transcode to `mjpeg` video codec, with a pixel
format `yuvj422p` - color values from 0-255 and only horizontal chroma resolution is halved.
For better performance (but less image accuracy...) you can choose `yuvj420p` , in that
case, both horizontal and vertical chroma resolution are halved. Finally it use a
video quantifier of `1` setting the quality scale (VBR) to best image quality.

In previous example, audio is muted using the `-an` flag, consequently you would start **veejay** with `-a0` to disable audio.

Optionally add PCM WAVE 16bit audio, 48.0 Khz, 2 channels, 8 bits per channel, HD resolution and 25 frames per second.
```shell
$ ffmpeg -i input-file -q:v 1 -c:v mjpeg -s 720p -r 25 -pix_fmt yuvj422p -acodec pcm_s16le -ar 48000 -ac 2 output-file.avi
```
For more detailled informations concerning audio, look at `README.audio.md`

Check FFmpeg documentation for detailed description of transcoding process and possible customization/automation.

### Or, you can use mplayer
```shell
$ mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -o <outputfile> <inputfile>
```

***To scale on the fly, use***
```shell
$ mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -vf scale=352:288 -o <outputfile> <inputfile>
```

***Bulk encoding***

A quick hint for bulk encoding a bunch of capture.dv files
```shell
$ for i in `ls *dv`;do mencoder -ovc lavc -oac pcm -lavcopts vcodec=mjpeg -o `echo $i | sed s/.dv/.avi/` $i; done;
```

Consult mplayer documentation about other options, such as cropping and filtering out blocks in video.

Going deeper in videos
------------
### What is Dummy mode ?
```shell
$ veejay -d
```

Dummy mode opens up a 'color stream' to start veejay without a video file.

If you use a video file, veejay will take that file's properties as default settings for the whole session.


### Virtual video device

You can create a fake v4l2 device (`/dev/videoX`), using [v42loopback](https://github.com/umlaeute/v4l2loopback).

Setup a single virtual video device on `/dev/video0` (assuming that it does not previously exist), stream some screen capture to it and open veejay using this source (audio disabled)
```shell
# modprobe v4l2loopback
$ ffmpeg -f x11grab -r 25 -s 720x576 -i :0.0+5,10 -vcodec rawvideo -pix_fmt yuv422p -f v4l2 /dev/video0
$ veejay -A1 -a0
```
Here, `0.0` is `display.screen` number of your X11 server, same as the DISPLAY environment variable and `5` is the x-offset and `10` the y-offset for the grabbing.
