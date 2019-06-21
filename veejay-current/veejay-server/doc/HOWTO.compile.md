
# Compiling Veejay 

Veejay is divided into multiple packages. Each must be build separately and in a specific order. 

1. [veejay-core](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-core) (__required__)
2. [veejay-server](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server) (__required__)
3. [veejay_client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client) (*optional*)
4. [veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils) (*optional*)
5. [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs) (*optional*)

## Prerequisities

Required:
* Build-essential
* FFmpeg (libavcodec, libavformat, libavutil, libswscale) *please use ffmpeg instead of libav*
* libjpeg
* libxml2 for saving project data
* SDL 2 for the video window
* libdv for playback of DV Video
* [http://www.gtk.org] GTK3 (3.22 recommanded)
* [http://www.gnome.org] GdkPixbuf (comes with Gnome)
* Cairo (needed for Reloaded)
* GtkCairo (needed for Reloaded)
* Libquicktime for Quicktime]
* Video4Linux II
* libpthread
* Glade (needed for Reloaded)

Optional:
* liblo
* DirectFB for secundary head (TVOut)
* Jack for audio playback
* G'MIC - GREYC's Magic for Image Computing
* libqrencode (Android VJ remote control)

First, make sure you system is up-to-date, and install (in debian like system) the required dependencies with:
```bash
sudo apt-get install build-essential git autoconf automake libtool m4 gcc libjpeg62-dev \
libswscale-dev libavutil-dev libavcodec-dev libavformat-dev libx11-dev  \
gtk-3.0-dev libxml2-dev libsdl2-dev libjack0 libjack-dev jackd1
```

## Quick and dirty build instructions

Normally, you can just run `./configure`.  
If you have cloned the veejay git respository, you will need to run `./autogen.sh` first to produce the configure file.


## Configure options

`PKG_CONFIG_PATH` is a environment variable that specifies additional paths in which `pkg-config` will search for its `.pc` files.

If you need additional paths, before running configure, check if the PKG_CONFIG_PATH variable is setup correctly:
```
$ echo $PKG_CONFIG_PATH
```
If echo is silent, you must set the PKG_CONFIG_PATH to point to the directory containing your additional .pc files (for homebrew library installation)


### Configure flags

`--enable-debug` Builds veejay for debugging purposes (disables optimization). See also `--enable-profiling`
`--with-arch-target=generic` Build veejay for generic x86 cpu-type. If the default is used (auto), the resulting binary may not run on another computer.
`--without-jack` Build veejay without sound suppport. Useful when you just care of visuals ouput. Veejay will have more ressources (cpu/memory) to play your videos.

You can see all the configure flags with `./configure --help`

## Building

1. Get the last sources from Veejay's repository:
```
  $ git clone git://github.com/c0ntrol/veejay.git veejay
```
2. Enter the source directory
```
  $ cd veejay/veejay-current
```
3. Move to current software to build and run autogen.sh
```
  $ cd veejay-core
  $ sh autogen.sh
```
3. Run ./configure
```
  $ ./configure
```
4. Type 'make' to build veejay
```
  $ make -j$(nproc)
```
5. Installing 
```
  $ sudo make install && sudo ldconfig
```
6. Repeat from step __3__ to __5__ inside `veejay-server` directory

7. *optional* continue with building `veejay-client` and `veejay-utils`
```
  $ cd veejay-client
  $ sh autogen.sh
  $ ./configure
  $ make && sudo make install

  $ cd veejay-utils
  $ sh autogen.sh
  $ ./configure
  $ make && sudo make install
```
8. *optional* continue with building the plugin-packs (repeat for all plugins pack you want to install)
```
  $ cd plugin-packs/lvdgmic
  $ sh autogen.sh
  $ ./configure
  $ make && sudo make install
  $ cd plugin-packs/lvdcrop
  $ sh autogen.sh
  $ ./configure
  $ make && sudo make install
```

## Test your build

You will start veejay, connect the user interface and quit veejay.

Test if veejay works:
```
  $ veejay -d -n
```

Start another terminal and type:
```
 $ reloaded -a
```

Open another terminal (depends on sayVIMS, build in step 7)
```
 $ sayVIMS -m "600:;"
```

(or press CTRL-C in the terminal running veejay)

