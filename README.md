Veejay
======

*a 'visual' instrument and realtime video sampler*

Veejay is a visual instrument and real-time video sampler.
It allows you to "play" the video like you would play a piano.
While playing, you can record the resulting video directly to disk (video sampling).


## Installation

Veejay is divided into multiple packages:

1. [veejay-server](./veejay/veejay-current/veejay-server)
1. [veejay-client](./veejay/veejay-current/veejay-client)
1. [veejay-utils](./veejay/veejay-current/veejay-utils)
1. [plugin-packs](./veejay/veejay-current/plugin-packs)

For each package, run the confgure and make:


```bash
 ./autogen.sh
 ./configure
 make && make install
```

If you want to build against libav-11 or later, you have to disable the resampler using `./configure --without-libresample` (this disables pitching up and down the audio) as veejay has not transitioned yet to the new API

If you want veejay to be optimized for the current cpu-type, you do not need to pass any parameters. If you don't now what cpu veejay will be running on , pass `--with-arch-target=auto` to configure.


Before running veejay, be sure to add/link some TrueType fonts in 

    $HOME/.veejay/fonts

## Usage

Running veejay is a much too large topic to cover in this readme. Various
pointers have been bundled with the sources in [veejay/veejay-current/veejay-server/doc](./veejay/veejay-current/veejay-server/doc)

Articles covering various aspects of "how to veejay" can be found on [veejayhq.net](http://veejayhq.net)

But the quick answer would be:

### 1. Start one or more Veejay servers:

```
veejay my-movie-A.avi
veejay -p 4490 my-movie-B.avi
```

### 2. Start the veejay graphical interface:

```
reloaded
```

## Building/Configuring plugins

Plugins enable additional video effects from various external sources.
to build plugins.

GMIC plugins:

```bash
cd plugin-packs/lvdgmic
./autogen.sh
./configure && make 
```

Veejay looks in a few common locations to find plugins. You can list more locations in $HOME/.veejay/plugins.cfg

You can change the default parameter values by editing the files in $HOME/.veejay/frei0r/ and $HOME/.veejay/livido/

## Debugging

if you want to debug veejay-server (or if you want to submit a meaningful backtrace), build with:

     ./configure --enable-debug



## FEATURE OVERVIEW

### General

 * Free Software (GNU GPL) (1)
 * Servent architecture (2)
 * Soft realtime (3)
 * Frame accurate (4)
 * Loop based editing (5)
 * Native YUV processing
 * Crash recovery

### Media

 * Codecs: MJPEG,MPNG, DV, YUV (raw)
 * Containers: AVI , Quicktime, rawDV
 * Devices: USB webcams, DV1394, TV capture cards, etc.
 * Support for unlimited capture devices
 * Support for Image files (PNG ,JPEG,TIFF,etc)

### Editing

 * 132 built-in FX , many unique and original FX filters 
 * 41 Livido filters
 * FX chain (20 slots)
 * All FX parameters can be animated.
 * Mix up to two layers per FX slot
 * Non destructive edit decision lists (cut/copy/paste/crop video)
 * Simple text editor 
 * Sample editor
 * Sequence editor
 * Live disk recorder (sampling)
 * Full deck save/restore
 * Live clip loading 
 * Live sample sequencing

### Trickplay

 * VIMS event recording/playback (6)
 * Various looping modes including bounce looping
 * Playback speed and direction
 * Video scratching
 * Change in-and out points of a sample (marker)
 * Slow motion audio / video (7)
 * Fast motion audio / video
 * Dynamic framerate 
 * Random frame play
 * Random sample play
 * Access up to 4096 video samples instantly	

### Output

 * Audio trough Jack (low latency audio server) (8)
 * SDL and OpenGL video
 * Headless
 * YUV4MPEG streaming
 * Network streaming (unicast and multicast)
 * Preview rendering

### Interaction

 * Programmable keyboard interface
 * VIMS (tcp/ip) 
 * OSC (udp)
 * PureData trough sendVIMS external

### Viewing

 * Full screen or windowed mode
 * Perspective and foward projection (9)
 

### Additional

 * Support for FreeFrame plugins (only for 32 bit systems!)
 * Support for Frei0r plugins
 * Support for LiVIDO plugins

## Contact / Feedback & HELP

Please join our mailing list on http://groups.google.com/group/veejay-discussion

## Bug Reporting

Please use the ticket system on https://github.com/c0ntrol/veejay/issues or simply write a mail to the veejay-discussion group!

ENJOY! And let us know about your performances/installations with veejay! 

