![Veejay banner](http://veejayhq.github.io/img/header.png)

## Veejay is a Visual Instrument

*a 'visual' instrument and realtime video sampler (for live video improvisation)*

It allows you to "play" the video like you would play a piano.

While playing, you can record the resulting video directly to disk (video sampling), all effects are realtime and optimized for use on modern processors.

Veejay likes the sound of your video's as much as their images: sound is kept in sync ( pitched when needed - trickplay) and delivered to [JACK](http://www.jackaudio.org/) for possible further processing.

You can cluster to allow a number of machines to work together over the network (uncompressed streaming, veejay chaining) And much more...

The engine is historically based upon mjpegtools's lavplay and processes all video in YUV planar It performs at its best, currently with MJPEG AVI (through ffmpeg/libav) or one of veejay's internal formats. Veejay is built upon a servent architecture.

see also : [README whatis](./veejay-current/veejay-server/doc/README.whatis.md)

### Veejay Applications:
* __Reloaded__
    A GUI developed in GLADE/GTK3 ([veejay-client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client))
* __sendVIMS__
    A PureData object allowing direct communications with the server ([sendVIMS](https://github.com/c0ntrol/veejay/tree/master/veejay-current/sendVIMS)) (_a bit outdated_)
* __sayVIMS__
    A console based utility for quick'n'dirty scripting ([veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils))

And of course [__Veejay__](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server) himself !  
The video output server (Ffmpeg/libSDL), a 'visual' instrument and realtime video sampler for live video improvisation such as live cinema, vjing, art installation ...

[//]: # ( comment : installation section duplicated in /veejay-server/doc/Instalation)
[//]: # ( WARNING : some URL/PATH have to be adapted )

## Installation

#### Get all the dependencies

First, make sure you system is up-to-date, and install the dependencies with:
```bash
sudo apt-get install build-essential autoconf automake libtool m4 gcc libjpeg62-dev \
libswscale-dev libavutil-dev libavcodec-dev libavformat-dev libx11-dev \
libxml2-dev libsdl2-dev libjack0 libjack-dev jackd1 libgtk3-dev libgdk-pixbuf-2.0-dev
```

#### Build the veejay's applications

Veejay is divided into multiple packages. Each must be build separately and in a specific order. 

1. [veejay-core](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-core) (__required__)
2. [veejay-server](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server) (__required__)
3. [veejay_client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client) (*optional*)
4. [veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils) (*optional*)
5. [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs) (*optional*)

For __each package__, run the triptich commands of the *GNU build system* (for a quick start you can build the first two):

```bash
 ./autogen.sh
 ./configure
 make -j$(nproc) && sudo make install
```
 __IMPORTANT :__ in some configuration you should have to __manually build__ the __shared libraries cache__ just after the __first veejay-core__ installation (ex `sudo ldconfig` or similar)

__Configure :__ You do not need to pass any parameters to `./configure` for veejay to be optimized with the current cpu-type.
If you want help to build for a specific architecture or with or without particular options (ex jack sound support) ... take a look to the `./configure --help` to adapt to many kinds of systems.

Before running veejay, be sure to add or link some TrueType fonts in `$HOME/.veejay/fonts`

Additional information about building veejay packages can be found in [HOWTO.compile.md](./veejay-current/veejay-server/doc/HOWTO.compile.md)

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

## Building and Configuring plugins

Veejay contain more than 160 built-in FX, many unique and original FX filters.  
But you can have more !

### frei0r

Veejay looks in a few common locations to find the [frei0r plugins pack](https://frei0r.dyne.org/):
* /usr/local/lib/frei0r-1
* /usr/lib/frei0r-1
* /usr/lib64/frei0r-1

You can list more location in the file `$HOME/.veejay/plugins.cfg`

### plugin-packs

There are several plugin-packs available for veejay: [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs)

* **lvdcrop** ; a couple of crop filters and a port of frei0r's scale0tilt
* **lvdshared** ; a couple of plugins that implement a producer/consumer mechanism for shared video resources
* **lvdgmic** ; GMIC based filters, although slow in processing they are quite amazing
* **lvdasciiart** ; let's do ascii ! ported from ffmpeg ASCII filter writen by Alexander Tumin

To compile and install a plugin-pack:
```bash
cd plugin-packs/lvdgmic
./autogen.sh
./configure
make && sudo make install
```

### Default parameter values

You can change the default FX parameter values by editing the files in `$HOME/.veejay/frei0r/` and `$HOME/.veejay/livido/`

**See Also** : For more verbose information about plugins and FX check [How to Plugins](./veejay-current/veejay-server/doc/HOWTO.plugins.md)

## Debugging

If you want to debug veejay-server (or if you want to submit a meaningful backtrace), build with:

     ./configure --enable-debug

see also : [How to debug](./veejay-current/veejay-server/doc/HOWTO.debugging.md)

[//]: # ( comment : END Instalation section duplicated in /veejay-server/doc/Instalation)
[//]: # ( WARNING : some URL/PATH have to be adapted )

## Quick Start & Play!

### Let's VJing Now...

[__Veejay Quick start and play!__](./veejay-current/veejay-server/doc/README.quickstart.md) : start veejay, manually send VIMS messages, tricks to video fifo or some essentials of keyboards user interaction...

## FEATURE OVERVIEW

[//]: # ( comment : BEGIN Feature section DUPLICATE in /veejay-server/doc/veejay-HOWTO.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

### General

 * Free Software (GNU GPL)
 * Servent architecture
 * Soft realtime
 * Frame accurate
 * Loop based editing
 * Native YUV processing
 * Crash recovery

see also : [YUV processing](./veejay-current/veejay-server/doc/YCbCr.txt), [README Memory](./veejay-current/veejay-server/doc/README.memory.md), [README Performance](./veejay-current/veejay-server/doc/README.performance.md)

### Media

 * Codecs: MJPEG, MPNG, DV, YUV (raw)
 * Containers: AVI, Quicktime, rawDV
 * Devices: USB webcams, DV1394, TV capture cards, etc.
 * Support for unlimited capture devices
 * Support for Image files (PNG ,JPEG, TIFF, etc)

see also : [README Video & Codecs](./veejay-current/veejay-server/doc/README.video-codec.md), [README audio](./veejay-current/veejay-server/doc/README.audio)

### FX processing

 * 161 built-in FX, many unique and original FX filters
 * 60 Livido filters
 * FX chain (20 slots) with Alpha Channels
 * All FX parameters can be animated
 * Mix up to two layers per FX slot

see also : [HOWTO plugins](./veejay-current/veejay-server/doc/HOWTO.plugins.md), [README alpha](./veejay-current/veejay-server/doc/README.alpha.md)

### Editing

 * Non destructive edit decision lists (cut/copy/paste/crop video)
 * Sample editor
 * Sequence editor
 * Live disk recorder (sampling)
 * Full deck save/restore
 * Live clip loading
 * Live sample sequencing

### Trickplay

 * VIMS event recording/playback
 * Various looping modes including bounce and random
 * Playback speed and direction
 * Video scratching
 * Change in-and out points of a sample (marker)
 * Slow motion audio / video
 * Fast motion audio / video
 * Dynamic framerate
 * Random frame play
 * Random sample play
 * Access up to 4096 video samples instantly

### Output

 * Full screen or windowed mode
 * Perspective and foward projection
 * Audio trough Jack (low latency audio server)
 * SDL video
 * Headless (no video output)
 * YUV4MPEG streaming
 * V4L2 loopback devices
 * Network streaming (unicast and multicast)
 * Preview rendering
 * Image grabbing

see also : [README Network](./veejay-current/veejay-server/doc/README.network.md)

### Interaction

 * Programmable keyboard interface
 * VIMS (tcp/ip)
 * OSC (udp)
 * PureData trough sendVIMS external
 * MIDI

see also : [How to PureData](./veejay-current/veejay-server/doc/HowtoVeejay-PureData.html)

### Viewing

 * Full screen or windowed mode
 * Perspective and foward projection
 * Twinview/BigDesktop
 * Split-screen video wall

see also : [How to video wall](./veejay-current/veejay-server/doc/video-wall.md)

### Plugins and more...

 * Support for Frei0r plugins
 * Support for LiVIDO plugins
 * Support for FreeFrame plugins (only for 32 bit systems!)
 * Support for GMIC plugins
 * Android client!

see also : [How to Plugins](./veejay-current/veejay-server/doc/HOWTO.plugins.md), [README odroid-xu3](./veejay-current/veejay-server/doc/README.odroid-xu3)

[//]: # ( comment : END Feature section DUPLICATE in /veejay-server/doc/veejay-HOWTO.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

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
