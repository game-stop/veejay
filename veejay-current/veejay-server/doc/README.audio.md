                                                             
                                                             
       ,---,                                                 
      '  .' \                         ,---,  ,--,            
     /  ;    '.             ,--,    ,---.'|,--.'|    ,---.   
    :  :       \          ,'_ /|    |   | :|  |,    '   ,'\  
    :  |   /\   \    .--. |  | :    |   | |`--'_   /   /   | 
    |  :  ' ;.   : ,'_ /| :  . |  ,--.__| |,' ,'| .   ; ,. : 
    |  |  ;/  \   \|  ' | |  . . /   ,'   |'  | | '   | |: : 
    '  :  | \  \ ,'|  | ' |  | |.   '  /  ||  | : '   | .; : 
    |  |  '  '--'  :  | : ;  ; |'   ; |:  |'  : |_|   :    | 
    |  :  :        '  :  `--'   \   | '/  '|  | '.'\   \  /  
    |  | ,'        :  ,      .-./   :    :|;  :    ;`----'   
    `--''           `--`----'    \   \  /  |  ,   /          
                                  `----'    ---`-'           
                                                             


AUDIO support
-------------

Veejay has limited audio support aka no fancy effects on sound. Despite that,
veejay will keep in sync the audio channel's of your clip, and transport it trough
[JACK - jackaudio.org](http://jackaudio.org/) a famous and robust low latency audio server.

Be sure veejay has NOT been configured without jack support (default jack support is enabled)

    $ veejay -B|grep -i jack
If the previous command is mute, you should configure and build veejay-server again.

Veejay only has support for jackd1 (the old jack).

Prepare
-------

You need an AVI file with an audio track encoded in signed PCM WAVE, 44-48Khz, 2 channels (stereo) (16 bit)

For example with ffmpeg/avconv you can use the "PCM signed 16-bit little-endian" codec named "pcm_s16le"

    $ ffmpeg -i myvideo.mp4 -q:v 1 -vcodec mjpeg -acodec pcm_s16le -ar 48000 -s 1024x576 myvideo.avi

Your input video `myvideo.mp4` will be transcode to `mjpeg` video codec, using a video quantifier
of `1` setting the quality scale (VBR) to best image quality. The `pcm_s16le` audio codec is
selected, for a `48khz` sampling rate. Finally the output video `myvideo.avi` is resized to `1024x576`

Run
---

When you start veejay with a video containing an audio track, the jackd server is automatically started
with the default settings. But you may want to have more control.

### Start Jack audio

From terminal command, start `jackd` :

    $ jackd -dalsa -P -r48000

__Important__ : Jack audio server must be configured with the same sampling rate
as the loaded video files. Consequently, all your video __files must have the same sampling rate__.

* 44khz audio files using `-r44100` into the previous command line
* 48khz audio files using `-r48000`

(An alternative to this to use the `qjackctl` graphical user interface)

### Now, start veejay

    $ veejay -m80 /path/to/myvideo.avi

Use the `-m` command line option to allow veejay to cache video frames when sampling. This will reduce disk latency,
allowing for smoother audio playback (especially when used in combination of speed/pitch changes)

The `-m` command line option is further explained in [README memory](./README.memory.md)

__Others audio related options__

    -a/--audio [01]            Enable ( 1 ) or disable ( 0 ) audio. By default, audio is enabled.
       --pace-correction [ms]  Audio pace correction offset in milliseconds
    -c/--synchronization [01]  Sync correction off/on (default on)
    -r/--audiorate <num>       Set audio rate (defaults to 48Khz)

__Nota__ : The video you start veejay with must contains an audio track, else the jack transport is not started ... even with `-a1` option given.

Audio playback problems
-----------------------

Q: What can I do about the warning "Rendering audio/video frame takes too long (measured 44 ms). Can't keep pace with audio!" 


A(1): You can start veejay with the -m commandline option. The warnings are less frequent or dissapear when the whole sample is cached in memory.

A(2): You can disable veejay's multithreaded pixel operations:
  
    $ export VEEJAY_MULTITHREAD_TASKS=0
    $ veejay -m80 /path/to/mjpeg.avi

A(3): Have a dedicated veejay-server machine and connect with reloaded through the network

A(4): Run with a different video codec or lower the video resolution
