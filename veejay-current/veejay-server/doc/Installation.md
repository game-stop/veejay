
[//]: # ( comment : BEGIN installation section duplicated in /README.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

## Get all the dependencies

First, make sure you system is up-to-date, and install (in debian like system) the required dependencies with:
```bash
sudo apt-get install build-essential autoconf automake libtool m4 gcc libjpeg62-dev \
libswscale-dev libavutil-dev libavcodec-dev libavformat-dev libx11-dev  \
gtk-3.0-dev libxml2-dev libsdl2-dev libjack0 libjack-dev jackd1
```

## Build the veejay's applications

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

Additional information about building and configuring veejay packages can be found in [HOWTO.compile.md](./HOWTO.compile.md)

## Usage

Running veejay is a much too large topic to cover in this readme. Various
pointers have been bundled with the sources in [veejay/veejay-current/veejay-server/doc](./)

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

**See Also** : For more verbose information about plugins and FX check [How to Plugins](./HowtoPlugins.md)

## Debugging

If you want to debug veejay-server (or if you want to submit a meaningful backtrace), build with:

     ./configure --enable-debug

see also : [How to debug](./HowToDebugging.txt)


[//]: # ( comment : END installation section duplicated in /README.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )
