
[//]: # ( comment : installation section duplicated from /README.md)

## Installation

Veejay is divided into multiple packages. Each must be build separately and in a specific order. 

1. [veejay-server](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server)
2. [veejay-client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client)
3. [veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils)
4. [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs)

For each package, run confgure and make:


```bash
 ./autogen.sh
 ./configure
 make && make install
```

If you want veejay to be optimized for the current cpu-type, you do not need to pass any parameters. If you do not know what cpu veejay will be running on , pass `--with-arch-target=auto` to configure.


Before running veejay, be sure to add/link some TrueType fonts in 

    $HOME/.veejay/fonts

## Usage

Running veejay is a much too large topic to cover in this readme. Various
pointers have been bundled with the sources in [veejay/veejay-current/veejay-server/doc](./veejay-current/veejay-server/doc)

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

There are several plugin-packs available for veejay: https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs 

* lvdcrop ; a couple of crop filters and a port of frei0r's scale0tilt 
* lvdshared ; a couple of plugins that implement a producer/consumer mechanism for shared video resources
* lvdgmic ; a couple of GMIC based filters, although slow in processing they are quite amazing

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
