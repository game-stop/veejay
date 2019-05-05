![Veejay banner](http://veejayhq.github.io/img/header.png)

## Veejay is a Visual Instrument

*a 'visual' instrument and realtime video sampler (for live video improvisation)*

It allows you to "play" the video like you would play a piano

While playing, you can record the resulting video directly to disk (video sampling), all effects are realtime and optimized for use on modern processors, Veejay likes the sound of your video's as much as their images: sound is kept in sync ( pitched when needed - trickplay) and delivered to JACK for possible further processing.

You can cluster to allow a number of machines to work together over the network (uncompressed streaming, veejay chaining) And much more...

The engine is historically based upon mjpegtools's lavplay and processes all video in YUV planar It performs at its best, currently with MJPEG AVI (through ffmpeg/libav) or one of veejay's internal formats. Veejay is built upon a servent architecture.

### Veejay Applications:
* __Reloaded__
    A GUI developed in GLADE/GTK
* __sendVIMS__
    A PureData object allowing direct communications with the server
* __sayVIMS__
    A console based utility for quick'n'dirty scripting

[//]: # ( comment : installation section duplicated in /veejay-server/doc/Instalation)

## Installation

Veejay is divided into multiple packages. Each must be build separately and in a specific order. 

1. [veejay-server](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server)
2. [reloaded-gtk3](https://github.com/c0ntrol/veejay/tree/master/veejay-current/reloaded-gtk3) or [veejay-client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client) for GTK2
3. [veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils)
4. [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs)

For each package, run confgure and make:

```bash
 ./autogen.sh
 ./configure
 make && sudo make install
```

If you want veejay to be optimized for the current cpu-type, you do not need to pass any parameters. 

Before running veejay, be sure to add or link some TrueType fonts in 

    $HOME/.veejay/fonts

## Usage

Running veejay is a much too large topic to cover in this readme. Various
pointers have been bundled with the sources in [veejay/veejay-current/veejay-server/doc](./veejay-current/veejay-server/doc)

Articles covering various aspects of "how to veejay" can be found on [veejayhq.net](http://veejayhq.net)

But the quick answer would be:

### 1. Start one or more Veejay servers:

```
veejay --clip-as-sample my-movie-A.avi
veejay -p 4490 -g my-movie-B.avi
```

### 2. Start and autoconnect the veejay graphical interface:

```
reloaded -a
```

## Building/Configuring plugins

There are several plugin-packs available for veejay: https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs 

* lvdcrop ; a couple of crop filters and a port of frei0r's scale0tilt 
* lvdshared ; a couple of plugins that implement a producer/consumer mechanism for shared video resources
* lvdgmic ; GMIC based filters, although slow in processing they are quite amazing

To compile and install a plugin-pack:
```bash
cd plugin-packs/lvdgmic
./autogen.sh
./configure && make 
```

Veejay looks in a few common locations to find plugins:
* /usr/local/lib/frei0r-1
* /usr/lib/frei0r-1
* /usr/lib64/frei0r-1

You can list more locations in $HOME/.veejay/plugins.cfg

You can change the default parameter values by editing the files in $HOME/.veejay/frei0r/ and $HOME/.veejay/livido/

## Debugging

if you want to debug veejay-server (or if you want to submit a meaningful backtrace), build with:

     ./configure --enable-debug

[//]: # ( comment : END installation section duplicated in /veejay-server/doc/Instalation)


## FEATURE OVERVIEW

### General

 * Free Software (GNU GPL) (1)
 * Servent architecture (2)
 * Soft realtime (3)
 * Frame accurate (4)
 * Loop based editing (5)
 * Native YUV(A) processing
 * Crash recovery

### Media

 * Codecs: MJPEG,MPNG, DV, YUV (raw)
 * Containers: AVI , Quicktime, rawDV
 * Devices: USB webcams, DV1394, TV capture cards, etc.
 * Support for unlimited capture devices
 * Support for Image files (PNG ,JPEG,TIFF,etc)

### Editing

 * 161 built-in FX , many unique and original FX filters 
 * 60 Livido filters
 * FX chain (20 slots) with Alpha Channels
 * All FX parameters can be animated.
 * Mix up to two layers per FX slot
 * Non destructive edit decision lists (cut/copy/paste/crop video)
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
 * SDL video
 * Headless (no output)
 * YUV4MPEG streaming
 * V4L2 loopback devices
 * Network streaming (unicast and multicast)
 * Image grabbing

### Interaction

 * Programmable keyboard interface
 * VIMS (tcp/ip) 
 * OSC (udp)
 * PureData trough sendVIMS external
 * MIDI 

### Viewing

 * Full screen or windowed mode
 * Perspective and foward projection (9)
 * Twinview/BigDesktop
 * Split-screen video wall
 

### Additional

 * Support for Frei0r plugins
 * Support for LiVIDO plugins
 * Support for FreeFrame plugins (only for 32 bit systems!)

## Contact / Feedback & HELP

Please join our mailing list on http://groups.google.com/group/veejay-discussion

## Bug Reporting

Please use the ticket system on https://github.com/c0ntrol/veejay/issues or simply write a mail to the veejay-discussion group!

ENJOY! And let us know about your performances/installations with veejay! 

---
```
                 _             _                            _   
                (_)           | |                          | |  
 __   _____  ___ _  __ _ _   _| |__   __ _       _ __   ___| |_ 
 \ \ / / _ \/ _ | |/ _` | | | | '_ \ / _` |     | '_ \ / _ | __|
  \ V |  __|  __| | (_| | |_| | | | | (_| |  _  | | | |  __| |_ 
   \_/ \___|\___| |\__,_|\__, |_| |_|\__, | (_) |_| |_|\___|\__|
               _/ |       __/ |         | |                     
              |__/       |___/          |_|                                      http://veejayhq.net
```
---


