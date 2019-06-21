Veejay HOWTO
======

Matthijs van Henten ( cola(AT)cb3rob(dot)net )  
Niels Elburg ( nielselburg(AT)yahoo(dot)de )  
v1.0, 30 March 2003  
v1.1, 22 June 2003  
v1.2, 26 August 2003  
v1.3, 9 November 2003  
v1.4, 24 May 2004  
v2.0, 25 July 2004  
v2.1 27 January 2005  
v2.2 22 April 2005  
v3.0 8 March 2008  

------------------------------------------------------------------------

*This document describes how to use **veejay**, a visual 'music'
instrument for Linux/GNU*

------------------------------------------------------------------------

1. [Introduction](veejay-HOWTO.md#1)
    1. [Disclaimer](veejay-HOWTO.md#1.1)
    1. [Acknowledgements](veejay-HOWTO.md#1.2)
    1. [Audience and Intent](veejay-HOWTO.md#1.3)
    1. [Revision History](veejay-HOWTO.md#1.4)
    1. [New versions of this document](veejay-HOWTO.md#1.5)
    1. [Feedback](veejay-HOWTO.md#1.6)
    1. [Distribution Policy](veejay-HOWTO.md#1.7)
1. [About veejay](veejay-HOWTO.md#2)
    1. [Features](veejay-HOWTO.md#2.1)
    1. [Hardware configuration](veejay-HOWTO.md#2.2)
1. [Installation](veejay-HOWTO.md#3)
    1. [Dependencies](veejay-HOWTO.md#3.1)
    1. [Installing from archive](veejay-HOWTO.md#3.2)
    1. [Installing from repository](veejay-HOWTO.md#3.3)
    1. [Setting up multicast](veejay-HOWTO.md#3.4)
1. [Using Veejay](veejay-HOWTO.md#4)
    1. [Terminology and limitations](veejay-HOWTO.md#4.1)
    1. [VIMS](veejay-HOWTO.md#4.2)
        1. [Global view](veejay-HOWTO.md#4.2.1)
        1. [sayVIMS utility](veejay-HOWTO.md#4.2.2)
    1. [The keyboard interface](veejay-HOWTO.md#4.3)
    1. [Recording video](veejay-HOWTO.md#4.4)
    1. [Streaming video](veejay-HOWTO.md#4.5)
1. [Popular packages](veejay-HOWTO.md#5)
    1. [EffecTV](veejay-HOWTO.md#5.1)
    1. [mplayer](veejay-HOWTO.md#5.2)
    1. [The MJPEG Tools](veejay-HOWTO.md#5.3)
    1. [Transcode](veejay-HOWTO.md#5.4)
1. [Other Resources](veejay-HOWTO.md#6)
    1. [Web Sites](veejay-HOWTO.md#6.1)
    1. [Mailing Lists](veejay-HOWTO.md#6.2)
1. [Credits](veejay-HOWTO.md#7)
1. [GNU Free Documentation License](veejay-HOWTO.md#8)

------------------------------------------------------------------------

<span id="1">1. Introduction</span>
-----------------------------------

<span id="1.1">1.1 Disclaimer</span>
------------------------------------

No liability for the contents of this documents can be accepted. Use the
concepts, examples and other content at your own risk. As this is a new
edition of this document, there may be errors and inaccuracies, that may
of course be damaging to your system. Proceed with caution, and although
this is highly unlikely, the authors do not take any responsibility for
that.  
All copyrights are held by their respective owners, unless specifically
noted otherwise. Use of a term in this document should not be regarded
as affecting the validity of any trademark or service mark.  
Naming of particular products or brands should not be seen as
endorsements.  
You are strongly recommended to take a backup of your system before
major installation and backups at regular intervals.

<span id="1.2">1.2 Acknowledgements</span>
------------------------------------------

The following peope have been helpful in getting this HOWTO done:

-   Matthijs van Henten ( <cola(AT)cb3rob(dot)net> )

<span id="1.3">1.3 Audience and Intent</span>
---------------------------------------------

This document is targeted at the Linux user interested in learning a bit
about veejay and trying it out.

<span id="1.4">1.4 Revision History</span>
------------------------------------------

**Version 1.0** First version for public release</br>
**Version 1.1** Updated Howto to match version 0.4.0. Revised chapters 2.2,4.4, 5.3 and
6</br>
**Version 1.2** Updated Howto to match version 0.4.6. Revised chapter 2.1,2.2,3.3</br>
**Version 1.3** Updated Howto to match version 0.5.3.</br>
**Version 1.4** Updated Howto to match version 0.5.9</br>
**Version 2.0** Partial re-write to match version 0.6</br>
**Version 2.1** Updated Howto to match version 0.7.2</br>
**Version 2.2** Updated Howto to match version 0.8</br>
**Version 3.0** Large rewrite to match version 1.1</br>

<span id="1.5">1.5 New versions of this document</span>
-------------------------------------------------------

You will find the most recent version of this document at actual code repository :  
[github -> veejay -> doc -> veejay-HOWTO.md](https://github.com/c0ntrol/veejay/blob/master/veejay-current/veejay-server/doc/veejay-HOWTO.md)

If you make a translation of this document into another langauge, let us
know and we'll include a reference to it here.

<span id="1.6">1.6 Feedback</span>
----------------------------------

We rely on you, the reader, to make this HOWTO usefull. If you have any
suggestions, corrections, or comments, translations, please send them to us (
[veejay-users@lists.sourceforge.net](veejay-users@lists.sourceforge.net)
), and we will try to incorporate them in the next revision. Please add
'HOWTO veejay' to the Subject-line of the mail.  

Before sending bug reports or questions, *please read all of the
information in this HOWTO,* and *send detailed information about the
problem*.  

If you publish this document on a CD-ROM or in hardcopy form, a
complimentary copy would be appreciated. Mail us for our postal address.
Also consider making a donation to the Veejay Project to help support
free video editing software in the future.

<span id="1.7">1.7 Distribution Policy</span>
---------------------------------------------

Permission is granted to copy, distribute and/or modify this document
under the terms of the GNU Free Documentation License, Version 1.1 or
any later version published by the Free Software Foundation; with no
Invariant Sections, with no Front-Cover Texts , and with no Back-Cover
Texts. A copy of this license is included in the section entitled "GNU
Free Documentation License".

<span id="2">2. About Veejay</span>
-----------------------------------

Veejay is a **visual instrument** and **realtime video sampler**. It
allows you to 'play' the video like you would play **a Piano** and it
allows you to record the resulting video directly to disk for immediate
playback (video sampling).  
  
Veejay consists out of several packages:

<table>
<tbody>
<tr class="odd">
<td><strong>veejay-server</strong></td>
<td>This is <strong>veejay</strong> !</td>
</tr>
<tr class="even">
<td><strong>veejay-client</strong></td>
<td><strong>reloaded</strong>, the graphical user interface to veejay</td>
</tr>
<tr class="odd">
<td><strong>veejay-utils</strong></td>
<td>Command-line interface with veejay, <strong>sayVIMS</strong></td>
</tr>
<tr class="even">
<td><strong>sendVIMS</strong></td>
<td>Simple VeeJay client for Pure Data</td>
</tr>
</tbody>
</table>

<span id="2.1">2.1 Features</span>
----------------------------------
[//]: # ( comment : BEGIN Feature section DUPLICATE in /README.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

### General

 * Free Software (GNU GPL)
 * Servent architecture
 * Soft realtime
 * Frame accurate
 * Loop based editing
 * Native YUV processing
 * Crash recovery

### Media

 * Codecs: MJPEG, MPNG, DV, YUV (raw)
 * Containers: AVI, Quicktime, rawDV
 * Devices: USB webcams, DV1394, TV capture cards, etc.
 * Support for unlimited capture devices
 * Support for Image files (PNG ,JPEG, TIFF, etc)

### FX processing

 * 161 built-in FX, many unique and original FX filters
 * 60 Livido filters
 * FX chain (20 slots) with Alpha Channels
 * All FX parameters can be animated
 * Mix up to two layers per FX slot

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

### Interaction

 * Programmable keyboard interface
 * VIMS (tcp/ip)
 * OSC (udp)
 * PureData trough sendVIMS external
 * MIDI

### Viewing

 * Full screen or windowed mode
 * Perspective and foward projection
 * Twinview/BigDesktop
 * Split-screen video wall

### Plugins and more...

 * Support for Frei0r plugins
 * Support for LiVIDO plugins
 * Support for FreeFrame plugins (only for 32 bit systems!)
 * Support for GMIC plugins
 * Android client!

[//]: # ( comment : END Feature section DUPLICATE in /README.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

<span id="2.2">2.2</span> Hardware configuration
------------------------------------------------

Veejay requires at least a linux kernel 2.4.x, 2.6.x or later, a lot of
diskspace and a fast CPU. Depending on the speed of your machine, your
milage may vary. See the list below for a few systems veejay was
reported to work on :

-   An Intel Pentium 4 3.0 Ghz HT/512 MB DDR RAM with a ATI Radeon 9600
    XT
-   A dual celeron 400 Mhz/512 MB RAM with a voodoo3 and second pci
    card.
-   An Athlon 750 Mhz with voodoo3 and second pci card.
-   An Athlon 750 Mhz with Matrox G400 Dualhead(TVout using X11/SDL or
    DirectFB)
-   An Athlon 850 Mhz and Matrox G550 Dualhead( TVout support through
    DirectFB)
-   An Athlon XP 1600 Mhz and Matrox G550 Dualhead( TVout support
    through DirectFB)
-   A Pentium 4 2.2 Ghz and Matrox G550 Dualhead( TVout support through
    DirectFB)
-   A Pentium 4 3.0 Ghz and ATI Radeon 9600 XT/ (no TVout yet)
-   Sony Playstation 2 (MIPS, little endian) (but runs very slow +/- 20
    fps)


Video Editing requires a lot of diskspace, make sure you you have enough
diskspace available for your project. Real Time video editing require
even more, the video file codecs used must be full frame (only I‑frames).
If you are going to use the recording functions, make sure you have sufficient
free disk space available. Otherwise you are quite safe, veejay does not
change your original video or fill your harddisk with needless temporary files.
Neither does it waste your resources (unless you fill the effect chain
with a lot of effects)

<span id="3">3. Installation</span>
-----------------------------------

<span id="3.1">3.1 Dependencies</span>
--------------------------------------

Before you install Veejay, you should install the following software
packages. Although none of them is required, Veejay will be much less
usable without them.

-   (required) The XML C library 2 for gnome &gt;= 2.5.4
-   (required) ffmpeg (libavcodec, etc) &gt;= 0.50.0
-   (optional) libdv &gt;= 1.02
-   (optional) The SDL library &gt;= 2.0.0
-   (optional) JACK low latency audio server &gt;= 0.98.1
-   (optional) DirectFB &gt;= 0.9.17
-   (optional) FreeType &gt;= 2.1.9
-   (optional) GTK &gt;= 3.22.0
-   (optional) libqrencode &gt;= 3.4.2


*On newer distributions, some of the listed software is already
installed but you may be missing the -devel- packages! (especially on
redhat, suse and debdian systems!!)*

You can find the websites of these projects in [Other
Resources](veejay-HOWTO.md#6).


### <span id="3.1.0">3.1.0 From source</span>

You can compile the following packages from source if your distribution
does not include them :

-   libavcodec, libavutil, libswscale and libavformat from the FFmpeg project

#### FFmpeg

You can download the [ffmpeg](https://ffmpeg.org/) sources from the repository via
[https://git.ffmpeg.org/ffmpeg.git](https://git.ffmpeg.org/ffmpeg.git) After
downloading, run the configure script with the following options:
```
$ ./configure --enable-swscaler --enable-shared --enable-gpl
...
$ make
# make install
```
<span id="3.2">3.2 Installing from archive</span>
-------------------------------------------

From archive, you install a published version. Installing the last published version insure you to get last stable veejay. **Ideal for partying !**

Decompress and untar the file by typing:
```
$ tar -jxvf veejay-1.5.x.tar.bz2
```
Change to the directory containing veejay's source's and configure the build:
```
$ cd veejay-1.5.x/veejay-current/veejay-server

$ ./configure
```
On completion it will summarize the results of the `./configure` script.  
You can run `./configure --help` to view all configure options and tweak your installation.

Now, you can start building veejay (using all cores)
```
$ make -j$(nproc)
```
Followed by
```
# make install && ldconfig
```
For **full installation instruction** please refer to this [documentation](./Installation.md)

<span id="3.3">3.3 Installing from repository</span>
-------------------------------------------

From sources, you will install last veejay version, could be in no stable situation, **ideal for coding !**

The last Veejay's source code could be found here [github / veejay](https://github.com/c0ntrol/veejay).  
Download the last source code by typing :
```
$ git clone https://github.com/c0ntrol/veejay.git
```
Enter the directory containing veejay's source's :
```
$ cd veejay/veejay-current/veejay-server
```
You need to prepare and configure the build:
```
$ ./autogen.sh
$ ./configure
```
On completion it will summarize the results of the `./configure` script.  
You can run `./configure --help` to view all configure options and tweak your installation.

Now, you can start building veejay (using all cores)
```
$ make -j$(nproc)
```
Followed by
```
# make install && ldconfig
```
For **full installation instruction** please refer to this [documentation](./Installation.md)

<span id="3.4">3.4 Setting up multicast</span>
------------------------

Multicast is a technology that reduces network traffic by simultaneously
delivering a single stream of information to any interested recipient.

To enable multicast in Veejay, you must have enabled *IP multicast* in
your kernel configuration.

Finally you need to add a multicast route :
```
for 1 ethernet device:
# route add -net 224.0.0.0 netmask 255.255.255.0 dev eth0

for > 1 
# route add -net 224.0.0.0 netmask 255.255.255.0 gw 192.168.100.1 dev eth1
```

Next, Veejay can be started with the commandline flags
**-M/--multicast-osc** and/or **-V/--multicast-vims**

<span id="4">4. Using Veejay</span>
-----------------------------------

Veejay uses by default a [SDL window](http://libsdl.org/) to play the video. All the
keybinding in veejay depend on SDL; if you move your mouse over to the
SDL windows to focus it, you can press the keys explained in [4.3 The
keyboard interface](veejay-HOWTO.md#4.3).  
To use veejay in commandline style interface mode, see [sayVIMS](veejay-HOWTO.md##sayVIMS).

You don't even need a video file to use with veejay; you can run it
in dummy mode by using the `-d` commandline parameter:
```
$ veejay -d
```

To use the graphical client with veejay (`-a` for auto-connection):
```
$ reloaded -a
```

<span id="4.1">4.1 Terminology and limitations</span>
-----------------------------------------------------

Note that veejay runs in only 1 resolution at a time (depending on
the video dimensions of the first loaded movie). All movies loaded must
have identical properties when streaming video from veejay to another
veejay.

Veejay has a number of playback modes, each playback mode is unique and
defines more or less a different functionality.

<table>
<tbody>
<caption><strong>Playback modes in veejay</strong></caption>
<tr class="">
<td><strong>Mode</strong></td>
<td><strong>Description</strong></td>
<td><strong>Navigation</strong></td>
<td><strong>Looping</strong></td>
<td><strong>Speed</strong></td>
<td><strong>Effect Chain</strong></td>
</tr>
<tr class="odd">
<td>Plain</td>
<td>Default mode, playback of video</td>
<td>Yes</td>
<td>No</td>
<td>Yes</td>
<td>No</td>
</tr>
<tr class="odd">
<td>Sample</td>
<td>Sample mode, playback of video samples.</td>
<td>Yes</td>
<td>Yes</td>
<td>Yes</td>
<td>Yes</td>
</tr>
<tr class="odd">
<td>Tag</td>
<td>Tag mode, playback of video streams</td>
<td>No</td>
<td>No</td>
<td>No</td>
<td>Yes</td>
</tr>
</tbody>
</table>


<span id="4.2">4.2 VIMS</span>
------------------------------
### <span id="4.2.1">4.2.1 Global view</span>

**VIMS is Veejay's IMS (Internal Message System)**

[//]: # ( comment : BEGIN VIMS section DUPLICATE in /veejay-current/veejay-server/doc/HOWTO)
[//]: # ( WARNING : section numbering have to be adapted )

Veejay is server/client model and all control data is distributed via VIMS. Each (atomical) message consists of an _Action Identifier_ ( aka __selector__ ) 
and a list of zero or more _Arguments_ which can be used to control Video Clips, Video Streams, the Effect Chain and many other things.

VIMS allows events to be triggered through:

* SDL - Keyboard Event ([libsdl](https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer))
* OSC - Synthesizers, computers, and other multimedia devices network protocol ([Open Sound Control](https://en.wikipedia.org/wiki/Open_Sound_Control))
* IMP - Veejay Internal Message Protocol

Along this documention we will review what messages you can send to veejay through using VIMS, Keyboard Event an OSC.

To quickly see documentation generated by veejay on VIMS, using Effects and OSC, use the command :

    $ veejay -u -n |less

If there is an error in the documentation, you have found a bug in veejay and should [report it](https://github.com/c0ntrol/veejay/issues) :)

After [installing](./Installation.md) `veejay-utils` package, you will have a commandline utility `sayVIMS` that we will discuss in this documentation.

Inside of veejay's source package you will find a `test/` directory containing various examples demonstrating how to load an ActionFile, how to use Perl for batch like video processing and how to attach message bundles to keyboard events.

Enough presentation, let's go deep in veejay messaging.


Message Format
==================

A message is described as:

    <Action Identifer> : <Argument List> ;

Example:

    080:;
    099:0 0;

<Action Identifier>  
The action identifier is a 3 digit number describing a Network Event  
The colon is used to indicate the start of the Argument List and must be given.

<Argument List>  
The Argument List is described by a `printf()` style formatted template
which describes the number and type of arguments to be used.

The semicolon must be given to indicate the end of this message

Bundled Messages
====================

A message bundle is a special message that contains an ordered list of at least 1 or more messages. Each message is executed from left to right (first in, first out) while parsing the bundle.   

You can dynamically assign keybindings to trigger a bundle in reloaded ( Menu -> Preferences -> VIMS Bundles )

There is an example Action File that a number of bundles with keybindings:
```
$ veejay -v /path/to/video.avi -F test/livecinema/action-file.xml
```

The bundles can be triggered by the keys SHIFT + [ q,w,e,r,t,y,u,i,o,p,a,s,d,f,g,h,j,k,l,z,x,c,v,b,n,m ]

Structure of bundled message
============================

Example:
```
5032|BUN:002{361:0 3 56 230 93 0;361:0 4 1 7;}|
5033|BUN:003{361:0 3 56 230 93 0;361:0 4 1 7;361:0 5 1 7;}|
5034|BUN:003{361:0 3 56 230 93 0;361:0 4 1 7;361:0 5 1 8;}|
```

A message bundle is described as:
```
   BUN: <Number of Messages> {
      <Action Idenfifier> : <Argument List> ;
      <Action Identifier> : <Argument List> ;
      ...
      }
    ;
```

The token 'BUN:' indicates the start of a messaage bundle, the first 3 digit numeric
value represents the total number of messages in the bundle. The '{' symbol indicates
the start of a message block and is ended with '};' or just '}'.

Format of an Action File/Attaching Keys to Bundles
======================================================
```
<501 - 599> | <message bundle> |
```
The contents of some action file can be :
```
516|BUN:001{355:;}|
```

The message bundle BUN sends '355' for clear effect chain.  
This message bundle is attached to action identifier 516.

A key is attached to this function trough using the Client UI (Reloaded) or by using dynamic keymapping.

DYNAMIC KEYMAPPING:
==================
```
"083:516 <sdl symbol> <modifier> <optional arguments>;"
```

The message bundle can be attached to a key, for example `SHIFT + A` by sending
```
083:516 97 3;
```
Which attaches bundle '516' to SDL key '97' using a modifier '3', which is SHIFT.

Modifiers    : 0 = none, 1 = alt , 2 = ctrl,  3 = shift  
SDL Keys     : see `SDL_keycode.h` (somewhere like `/usr/include/SDL2/`)

If the number 0 is used for an event number, a given key combination can be
unset (wiped) :
```
083:0 97 3;
```
Alternativly, you can bind keys to any action identifier.  
The complete list can be viewd by typing `veejay -u |less` or with `reloaded` in `Menu/Preferences/VIMS Bundle`.

```
083:20 97 0 4;
```
The example above sets key 'a' to 'change video speed to 4'

General description of VIMS messages
=====================================

Some reserved numbers:
```
    clip id      0  :   select currently playing clip
    clip id     -1  :   select highest clip number
    chain entry -1  :   select current chain entry
    stream id    0  :   select currently playing stream
    stream id   -1  :   select highest stream number
    key modifier    :   0 = normal, 1= alt , 2 = ctrl, 3 = shift
    frame       -1  :   use highest possible frame number (usually num video frames)
    playback mode   :   0 = clip, 1 = stream, 2 = plain
    data format     :   yv16 (yuv 4:2:2 raw) , mpeg4, divx, msmpeg4v3,
                        div3, dvvideo, dvsd, mjpeg, i420 and yv12 (yuv 4:2:0 raw)
    loop type       :   0 = play once, 1 = normal loop, 2 = pingpong (bounce) loop, 3 = random frame, 4 = Play once (no pause)
```
[//]: # ( comment : END VIMS section DUPLICATE in /veejay-current/veejay-server/doc/VIMS)
[//]: # ( WARNING : section numbering have to be adapted )

For deeper information about VIMS have a look to [VIMS.md](./VIMS.md).

### <span id="4.2.2">4.2.2 sayVIMS utility</span>

[//]: # ( comment : BEGIN sayVIMS section DUPLICATE in /veejay-current/veejay-server/doc/VIMS)
[//]: # ( WARNING : url have to be adapted )

***sayVIMS*** is a commandline utility distributed with the veejay package. It allows you to send short commands in interactive mode, single VIMS message or files containing VIMS messages to batch-process to veejay.
```
Usage: sayVIMS [options] [messages]
where options are:
 -p          Veejay port (3490)
 -g          Veejay groupname (224.0.0.31)
 -h          Veejay hostname (localhost)
 -m          Send single message
 -i          Interactive mode
 -f file     Read from (special) file
 -d          Dump status to stdout
 -b          Base64 encode binary data
 -v          Verbose
 -?          Print this help
```

In the following examples, lets say a veejay instance is running on host
`localhost` using port `3490` (veejay default values).

**Interactive Mode**
```
$ sayVIMS -i -h localhost -p 3490
```
In interactive mode, after a connection is establish with a veejay server,
a prompt wait you to enter VIMS commands (see previous chapter about the VIMS message format). Simply press `[ENTER]` to send it. 

```
$ sayVIMS -i
veejay sayVIMS 1.1.8
    type 'quit' or press CTRL-c to exit
    see 'veejay -u' for a list of commands
017:;
```
Here, the `017` tells veejay to go to sample starting position.

The connection remain open until you exit by typing `quit` or hit  `[Ctrl]-[c]`.

**Single VIMS message**

Also, you can send single VIMS format message.

For example, add the Pixelate effect on the Effect Chain of the current
playing stream or clip :
```
$ sayVIMS -h localhost -p 3490 -m "361:0 0 100 3;"
```

**Using files**

Last but not least, sayVIMS can parse files containing VIMS messages.

See the `veejay-server/test/examples` directory of the package for a list of perl
scripts that output a VIMS script.
```
$ sayVIMS -f advocate.vims -h localhost -p 3490
```

**Others examples**

Alternatively, you can start a secundary veejay and stream from peer to
peer (TCP) in uncompressed video:
```
$ veejay -d -p 5000
$ sayVIMS -h localhost -p 5000 -m "245:localhost 3490;"

(press 'F7' in veejay to display the stream, prob. stream 7)
```
Or for multicast (UDP):
```
$ veejay -V 224.0.0.50 -p 5000 -n -L movie1.avi
$ veejay -d
$ sayVIMS -h localhost -p 3490 -m "246:224.0.0.50 5000;"
$ veejay -d -p 4000
$ sayVIMS -h localhost -p 4000 -m "246:224.0.0.50 5000;"
```
Have a look on [4.5.3 network](veejay-HOWTO.md#4.5.3) for more UDP multicasting.

[//]: # ( comment : END sayVIMS section DUPLICATE in /veejay-current/veejay-server/doc/VIMS)
[//]: # ( WARNING : url have to be adapted )

<span id="4.3">4.3 The keyboard interface</span>
------------------------------------------------

Here is a quick overview for the most used default keys, if applied in
order you will end up with a newly created video sample looping in some
way (depending on how many times you press the asterix key)

<table>
<tbody>
<caption><strong>Some keyboard bindings</strong></caption>
<tr class="">
<td><strong>Description</strong></td>
<td><strong>SDL key</strong></td>
<td><strong>In plain english</strong></td>
</tr>
<tr class="odd">
<td>Set the starting position of a new sample</td>
<td>SDLK_LEFTBRACKET</td>
<td>Left bracket</td>
</tr>
<tr class="odd">
<td>Set ending position and create a new sample</td>
<td>SDLK_RIGHTBRACKET</td>
<td>Right bracket</td>
</tr>
<tr class="odd">
<td>Select and play sample 1</td>
<td>SDLK_F1</td>
<td>F1</td>
</tr>
<tr class="odd">
<td>Set playback speed to 3</td>
<td>SDLK_d</td>
<td>d</td>
</tr>
<tr class="odd">
<td>Change looptype</td>
<td>SDLK_KP_MULTIPLY</td>
<td>asterix on numeric keypad</td>
<tr class="odd">
<td>Play backward</td>
<td>SDLK_KP_4</td>
<td>Cursor left on numeric keypad</td>
</tr>
<tr class="odd">
<td>Play forward</td>
<td>SDLK_KP_6</td>
<td>Cursor right on numeric keypad</td>
</tr>
<tr class="odd">
<td>Skip 1 second</td>
<td>SDLK_KP_8</td>
<td>Cursor up on numeric keypad</td>
</tr>
<tr class="odd">
<td>Switch playmode to Plain</td>
<td>SDLK_KP_DIVIDE</td>
<td>Divide on numeric keypad</td>
</tr>
<tr class="odd">
<td>Print information about sample</td>
<td>SDLK_HOME</td>
<td>Home</td>
</tr>
</tbody>
</table>

The function keys **F1**...**F12** can be used to select sample **1**
... **12**, use the keys **1**...**9** to select a sample range **1-12**
... **108-120** and press one of the **F**-keys to play that sample.  
  
Use **ESC** to switch between samples and streams. Press **ESC** again
to switch back to the sample playmode.

You can create new input streams by using the console interface **sayVIMS** or by
using the graphical user interface **reloaded**. All new input streams (and samples) are auto numbered.

<span id="4.4">4.4 Recording video</span>
-----------------------------------------

You can record video to a new clip, in a specified codec format, by using the stream or clip recorder functions.

For example, to select a MJPG codec to record a new clip from a playing clip:
```
$ sayVIMS -m "302:mjpg;"
```

Record 100 frames and start playing (1 = auto-play) new clip when ready:
```
$ sayVIMS -m "130:100 1;"
```

Record the whole clip and don't start playing new clip when ready:
```
$ sayVIMS -m "130:0 0;"
```

If your Effect Chain is very CPU demanding, consider disabling audio and using the command-line parameter `-c 0` to disable synchronization correction.

It is possible to start veejay headless and have it write all video data
to a (special) file for further processing. Refer to chapter [5.1](veejay-HOWTO.md#5.1) for some examples.

<span id="4.5">4.5 Streaming video</span>
-----------------------------------------

You can create an input stream to read video coming from a video4linux
device (USB webcam, TV tuner...), from a pipe or from a network socket (both unicast and multicast).

### <span id="4.5.1">4.5.1 video4linux</span>

To open a video4linux device use reloaded or type the command:
```
$ sayVIMS -m "240:0 1;"
```

The VIMS selector `240` tells veejay to open a video4linux device, the first
argument `0` indicates the device number (i.e. `/dev/video0`) and the last
argument `1` indicates the video in port of your capture card (in this
case composite).  
Veejay will create a new stream see [chapter 4.3](#4.3) for activating
the stream.

### <span id="4.5.2">4.5.2 pipe</span>

Veejay supports reading video data from a pipe (FIFO) by means of an
input stream.  
The only supported transport format is yuv4mpeg (yuv 4:2:0). When
playing YUV 4:2:2 the video stream will be sampled to YUV 4:2:0 and vice
versa.

You can create the input stream by typing the command
```
$ sayVIMS -m "243:/tmp/stream.yuv;"
```

### <span id="4.5.3">4.5.3 network</span>

#### TCP
To get frames from another running veejay, use the VIMS selector `245` with port and hostname (or IP number):
```
$ sayVIMS -m "245:5000 localhost;"
```

#### UDP
If you want to send the same video to multiple running veejays across
the network, you can save bandwith by starting the veejay you wish to
use as server with the `-V` option.

First, you need a multicast route in your routing table. See chapter
[3.4](#3.4) for a short introduction or consult a howto that discuss
on setting up multicast for your operating system.

You can use the `-V` option to start an optional multicast frame sender.
```
$ veejay -V 224.0.0.50 -p 5000 -L movie1.avi
```

Start another veejay in dummy mode, and use **sayVIMS** to create input stream:
```
$ veejay -d
$ sayVIMS -h localhost -p 3490 -m "246:5000 224.0.0.50;"
```
Start more veejays and use sayVIMS with
the `-p` option to give it a port offset number.

```
$ veejay -d -p 4000
$ sayVIMS -h localhost -p 4000 -m "246:224.0.0.50 5000;"
```
<span id="5">5 Popular Packages (outdated for archive only)</span>
--------------------------------------

Usefull software (in no apparant order):

-   The MJPEG Tools
-   Transcode
-   PureData (PD)
-   PDP for PD
-   Ffmpeg

Please refer to [Other Resources](veejay-HOWTO.md#6) to find the
project's website  

5.1 The MJPEG Tools
-------------------

The Mjpeg tools are a set of tools that can do recording of videos and
playback, simple cut-and-paste editing and the MPEG compression of audio
and video under Linux. You can use the EditLists from this package in
veejay and vice versa  
Here are a few examples for processing video data:  
  
1. Start veejay headless:

    $ mkfifo /tmp/special_file
    $ veejay /video/video.avi -O3 -o /tmp/special_file

  
Encoding it to DV avi type 2 (if video dimensions match either full PAL
or NTSC)

    $ cat /tmp/special_file | yuv2rawdv -v 2 > rawdv

  
Encoding it to MJPEG file 'video-mjpeg.avi'

    $ cat /tmp/special_file | yuv2lav -v2 -f 0 -I 0 -q 90 -o video-mjpeg.avi

Encoding veejay output to MJPEG file:

    $ veejay movie1.avi -o stdout -O3 | yuv2lav -f 0 -I 0 -q 90 -o movie1-mjpeg.avi

5.2 Transcode
-------------

Transcode is a Linux video Stream Processing Tool, it can convert
between different types of video formats  
  
Encode a file to mjpeg with no audio and rescale the output video to
352x288:  
  

    $ transcode -i input_file.avi -o new_mjpeg_file.avi -y mjpeg,null -Z352x288

  

<span id="6">6. Other Resources</span>
--------------------------------------

Here you will find the websites of the packages veejay requires as well
as packages you can use in combination with veejay.

<span id="6.1">6.1 Web Sites</span>
-----------------------------------

### Packages you need

-   [veejay - veejayhq.net](http://veejayhq.net)
-   [Quasar DV Codec - libdv.sourceforge.net/](http://libdv.sourceforge.net/)
-   [Simple DirectMedia Layer - libsdl.org](https://libsdl.org/)
-   [The XML C library for Gnome - xmlsoft.org](http://www.xmlsoft.org/)
-   [DirectFB - directfb.org](http://www.directfb.org) (outdated)
-   [JACK - jackaudio.org](http://jackaudio.org/)
-   [FreeType - freetype.org](https://www.freetype.org/)
-   [Ffmpeg - ffmpeg.org](https://ffmpeg.org)

### Usefull software

-   [Mplayer - mplayerhq.hu](http://www.mplayerhq.hu/)
-   [The MJPEGTools - mjpeg.sourceforge.net](http://mjpeg.sourceforge.net)
-   [Pure Data - puredata.info](https://puredata.info/)
-   [sendVIMS PD module (very cool!)](http://zwizwa.fartit.com/pd/sendVIMS/) (**now in veejay** see chapter [2](veejay-HOWTO.md#2))
-   [Transcode ](http://www.theorie.physik.uni-goettingen.de/~ostreich/transcode/) (outdated)

<span id="6.2">6.2 Mailing Lists</span>
---------------------------------------

There is a mailing list for veejay which is hosted by Sourceforge. The
address is <veejay-users@lists.sourceforge.net>

<span id="6.3">6.3 Veejay developer's lounge</span>
---------------------------------------------------

Veejay's developer lounge provides a [ticket system](https://github.com/c0ntrol/veejay/issues) for you , the user,
to report any problem or feature requests. The ticket system allows us
to keep track of problems.

Also, the [developer lounge](https://github.com/c0ntrol/veejay) hosts a git based code repository where you
can find the 'on the bleeding edge' source codes of veejay.

```
$ git clone https://github.com/c0ntrol/veejay.git
```

Many thanks to jaromil (author of FreeJ/Muse) and the [Dyne
Foundation](http://dyne.org) for previously providing, to the veejay project, those tools during many years !

<span id="7">7. Credits</span>
------------------------------

End of the Veejay HOWTO. (You can stop reading here.)

<span id="8">8. GNU Free Documentation License</span>
-----------------------------------------------------

GNU Free Documentation License

Version 1.1, March 2000

Copyright (C) 2000 Free Software Foundation, Inc. 59 Temple Place, Suite
330, Boston, MA 02111-1307 USA

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.

0. PREAMBLE

The purpose of this License is to make a manual, textbook, or other
written document "free" in the sense of freedom: to assure everyone the
effective freedom to copy and redistribute it, with or without modifying
it, either commercially or noncommercially. Secondarily, this License
preserves for the author and publisher a way to get credit for their
work, while not being considered responsible for modifications made by
others.

This License is a kind of "copyleft", which means that derivative works
of the document must themselves be free in the same sense. It
complements the GNU General Public License, which is a copyleft license
designed for free software.

We have designed this License in order to use it for manuals for free
software, because free software needs free documentation: a free program
should come with manuals providing the same freedoms that the software
does. But this License is not limited to software manuals; it can be
used for any textual work, regardless of subject matter or whether it is
published as a printed book. We recommend this License principally for
works whose purpose is instruction or reference.

1. APPLICABILITY AND DEFINITIONS

This License applies to any manual or other work that contains a notice
placed by the copyright holder saying it can be distributed under the
terms of this License. The "Document", below, refers to any such manual
or work. Any member of the public is a licensee, and is addressed as
"you".

A "Modified Version" of the Document means any work containing the
Document or a portion of it, either copied verbatim, or with
modifications and/or translated into another language.

A "Secondary Section" is a named appendix or a front-matter section of
the Document that deals exclusively with the relationship of the
publishers or authors of the Document to the Document's overall subject
(or to related matters) and contains nothing that could fall directly
within that overall subject. (For example, if the Document is in part a
textbook of mathematics, a Secondary Section may not explain any
mathematics.) The relationship could be a matter of historical
connection with the subject or with related matters, or of legal,
commercial, philosophical, ethical or political position regarding them.

The "Invariant Sections" are certain Secondary Sections whose titles are
designated, as being those of Invariant Sections, in the notice that
says that the Document is released under this License.

The "Cover Texts" are certain short passages of text that are listed, as
Front-Cover Texts or Back-Cover Texts, in the notice that says that the
Document is released under this License.

A "Transparent" copy of the Document means a machine-readable copy,
represented in a format whose specification is available to the general
public, whose contents can be viewed and edited directly and
straightforwardly with generic text editors or (for images composed of
pixels) generic paint programs or (for drawings) some widely available
drawing editor, and that is suitable for input to text formatters or for
automatic translation to a variety of formats suitable for input to text
formatters. A copy made in an otherwise Transparent file format whose
markup has been designed to thwart or discourage subsequent modification
by readers is not Transparent. A copy that is not "Transparent" is
called "Opaque".

Examples of suitable formats for Transparent copies include plain ASCII
without markup, Texinfo input format, LaTeX input format, SGML or XML
using a publicly available DTD, and standard-conforming simple HTML
designed for human modification. Opaque formats include PostScript, PDF,
proprietary formats that can be read and edited only by proprietary word
processors, SGML or XML for which the DTD and/or processing tools are
not generally available, and the machine-generated HTML produced by some
word processors for output purposes only.

The "Title Page" means, for a printed book, the title page itself, plus
such following pages as are needed to hold, legibly, the material this
License requires to appear in the title page. For works in formats which
do not have any title page as such, "Title Page" means the text near the
most prominent appearance of the work's title, preceding the beginning
of the body of the text.

2. VERBATIM COPYING

You may copy and distribute the Document in any medium, either
commercially or noncommercially, provided that this License, the
copyright notices, and the license notice saying this License applies to
the Document are reproduced in all copies, and that you add no other
conditions whatsoever to those of this License. You may not use
technical measures to obstruct or control the reading or further copying
of the copies you make or distribute. However, you may accept
compensation in exchange for copies. If you distribute a large enough
number of copies you must also follow the conditions in section 3.

You may also lend copies, under the same conditions stated above, and
you may publicly display copies.

3. COPYING IN QUANTITY

If you publish printed copies of the Document numbering more than 100,
and the Document's license notice requires Cover Texts, you must enclose
the copies in covers that carry, clearly and legibly, all these Cover
Texts: Front-Cover Texts on the front cover, and Back-Cover Texts on the
back cover. Both covers must also clearly and legibly identify you as
the publisher of these copies. The front cover must present the full
title with all words of the title equally prominent and visible. You may
add other material on the covers in addition. Copying with changes
limited to the covers, as long as they preserve the title of the
Document and satisfy these conditions, can be treated as verbatim
copying in other respects.

If the required texts for either cover are too voluminous to fit
legibly, you should put the first ones listed (as many as fit
reasonably) on the actual cover, and continue the rest onto adjacent
pages.

If you publish or distribute Opaque copies of the Document numbering
more than 100, you must either include a machine-readable Transparent
copy along with each Opaque copy, or state in or with each Opaque copy a
publicly-accessible computer-network location containing a complete
Transparent copy of the Document, free of added material, which the
general network-using public has access to download anonymously at no
charge using public-standard network protocols. If you use the latter
option, you must take reasonably prudent steps, when you begin
distribution of Opaque copies in quantity, to ensure that this
Transparent copy will remain thus accessible at the stated location
until at least one year after the last time you distribute an Opaque
copy (directly or through your agents or retailers) of that edition to
the public.

It is requested, but not required, that you contact the authors of the
Document well before redistributing any large number of copies, to give
them a chance to provide you with an updated version of the Document.

4. MODIFICATIONS

You may copy and distribute a Modified Version of the Document under the
conditions of sections 2 and 3 above, provided that you release the
Modified Version under precisely this License, with the Modified Version
filling the role of the Document, thus licensing distribution and
modification of the Modified Version to whoever possesses a copy of it.
In addition, you must do these things in the Modified Version:

A. Use in the Title Page (and on the covers, if any) a title distinct
from that of the Document, and from those of previous versions (which
should, if there were any, be listed in the History section of the
Document). You may use the same title as a previous version if the
original publisher of that version gives permission. B. List on the
Title Page, as authors, one or more persons or entities responsible for
authorship of the modifications in the Modified Version, together with
at least five of the principal authors of the Document (all of its
principal authors, if it has less than five). C. State on the Title page
the name of the publisher of the Modified Version, as the publisher. D.
Preserve all the copyright notices of the Document. E. Add an
appropriate copyright notice for your modifications adjacent to the
other copyright notices. F. Include, immediately after the copyright
notices, a license notice giving the public permission to use the
Modified Version under the terms of this License, in the form shown in
the Addendum below. G. Preserve in that license notice the full lists of
Invariant Sections and required Cover Texts given in the Document's
license notice. H. Include an unaltered copy of this License. I.
Preserve the section entitled "History", and its title, and add to it an
item stating at least the title, year, new authors, and publisher of the
Modified Version as given on the Title Page. If there is no section
entitled "History" in the Document, create one stating the title, year,
authors, and publisher of the Document as given on its Title Page, then
add an item describing the Modified Version as stated in the previous
sentence. J. Preserve the network location, if any, given in the
Document for public access to a Transparent copy of the Document, and
likewise the network locations given in the Document for previous
versions it was based on. These may be placed in the "History" section.
You may omit a network location for a work that was published at least
four years before the Document itself, or if the original publisher of
the version it refers to gives permission. K. In any section entitled
"Acknowledgements" or "Dedications", preserve the section's title, and
preserve in the section all the substance and tone of each of the
contributor acknowledgements and/or dedications given therein. L.
Preserve all the Invariant Sections of the Document, unaltered in their
text and in their titles. Section numbers or the equivalent are not
considered part of the section titles. M. Delete any section entitled
"Endorsements". Such a section may not be included in the Modified
Version. N. Do not retitle any existing section as "Endorsements" or to
conflict in title with any Invariant Section.

If the Modified Version includes new front-matter sections or appendices
that qualify as Secondary Sections and contain no material copied from
the Document, you may at your option designate some or all of these
sections as invariant. To do this, add their titles to the list of
Invariant Sections in the Modified Version's license notice. These
titles must be distinct from any other section titles.

You may add a section entitled "Endorsements", provided it contains
nothing but endorsements of your Modified Version by various parties-for
example, statements of peer review or that the text has been approved by
an organization as the authoritative definition of a standard.

You may add a passage of up to five words as a Front-Cover Text, and a
passage of up to 25 words as a Back-Cover Text, to the end of the list
of Cover Texts in the Modified Version. Only one passage of Front-Cover
Text and one of Back-Cover Text may be added by (or through arrangements
made by) any one entity. If the Document already includes a cover text
for the same cover, previously added by you or by arrangement made by
the same entity you are acting on behalf of, you may not add another;
but you may replace the old one, on explicit permission from the
previous publisher that added the old one.

The author(s) and publisher(s) of the Document do not by this License
give permission to use their names for publicity for or to assert or
imply endorsement of any Modified Version.

5. COMBINING DOCUMENTS

You may combine the Document with other documents released under this
License, under the terms defined in section 4 above for modified
versions, provided that you include in the combination all of the
Invariant Sections of all of the original documents, unmodified, and
list them all as Invariant Sections of your combined work in its license
notice.

The combined work need only contain one copy of this License, and
multiple identical Invariant Sections may be replaced with a single
copy. If there are multiple Invariant Sections with the same name but
different contents, make the title of each such section unique by adding
at the end of it, in parentheses, the name of the original author or
publisher of that section if known, or else a unique number. Make the
same adjustment to the section titles in the list of Invariant Sections
in the license notice of the combined work.

In the combination, you must combine any sections entitled "History" in
the various original documents, forming one section entitled "History";
likewise combine any sections entitled "Acknowledgements", and any
sections entitled "Dedications". You must delete all sections entitled
"Endorsements."

6. COLLECTIONS OF DOCUMENTS

You may make a collection consisting of the Document and other documents
released under this License, and replace the individual copies of this
License in the various documents with a single copy that is included in
the collection, provided that you follow the rules of this License for
verbatim copying of each of the documents in all other respects.

You may extract a single document from such a collection, and distribute
it individually under this License, provided you insert a copy of this
License into the extracted document, and follow this License in all
other respects regarding verbatim copying of that document.

7. AGGREGATION WITH INDEPENDENT WORKS

A compilation of the Document or its derivatives with other separate and
independent documents or works, in or on a volume of a storage or
distribution medium, does not as a whole count as a Modified Version of
the Document, provided no compilation copyright is claimed for the
compilation. Such a compilation is called an "aggregate", and this
License does not apply to the other self-contained works thus compiled
with the Document, on account of their being thus compiled, if they are
not themselves derivative works of the Document.

If the Cover Text requirement of section 3 is applicable to these copies
of the Document, then if the Document is less than one quarter of the
entire aggregate, the Document's Cover Texts may be placed on covers
that surround only the Document within the aggregate. Otherwise they
must appear on covers around the whole aggregate.

8. TRANSLATION

Translation is considered a kind of modification, so you may distribute
translations of the Document under the terms of section 4. Replacing
Invariant Sections with translations requires special permission from
their copyright holders, but you may include translations of some or all
Invariant Sections in addition to the original versions of these
Invariant Sections. You may include a translation of this License
provided that you also include the original English version of this
License. In case of a disagreement between the translation and the
original English version of this License, the original English version
will prevail.

9. TERMINATION

You may not copy, modify, sublicense, or distribute the Document except
as expressly provided for under this License. Any other attempt to copy,
modify, sublicense or distribute the Document is void, and will
automatically terminate your rights under this License. However, parties
who have received copies, or rights, from you under this License will
not have their licenses terminated so long as such parties remain in
full compliance.

10. FUTURE REVISIONS OF THIS LICENSE

The Free Software Foundation may publish new, revised versions of the
GNU Free Documentation License from time to time. Such new versions will
be similar in spirit to the present version, but may differ in detail to
address new problems or concerns. See http://www.gnu.org/copyleft/.

Each version of the License is given a distinguishing version number. If
the Document specifies that a particular numbered version of this
License "or any later version" applies to it, you have the option of
following the terms and conditions either of that specified version or
of any later version that has been published (not as a draft) by the
Free Software Foundation. If the Document does not specify a version
number of this License, you may choose any version ever published (not
as a draft) by the Free Software Foundation.

ADDENDUM: How to use this License for your documents

To use this License in a document you have written, include a copy of
the License in the document and put the following copyright and license
notices just after the title page:  

        Copyright (c)  YEAR  YOUR NAME.
        Permission is granted to copy, distribute and/or modify this document
        under the terms of the GNU Free Documentation License, Version 1.1
        or any later version published by the Free Software Foundation;
        with the Invariant Sections being LIST THEIR TITLES, with the
        Front-Cover Texts being LIST, and with the Back-Cover Texts being LIST.
        A copy of the license is included in the section entitled "GNU
        Free Documentation License".

If you have no Invariant Sections, write "with no Invariant Sections"
instead of saying which ones are invariant. If you have no Front-Cover
Texts, write "no Front-Cover Texts" instead of "Front-Cover Texts being
LIST"; likewise for Back-Cover Texts.

If your document contains nontrivial examples of program code, we
recommend releasing these examples in parallel under your choice of free
software license, such as the GNU General Public License, to permit
their use in free software.
