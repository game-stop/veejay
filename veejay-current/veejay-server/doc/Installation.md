
[//]: # ( comment : END installation section duplicated in /README.md)
[//]: # ( WARNING : some URL/PATH have to be adapted )

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
__Nota :__ in some configuration you will have to rebuild the shared libraries cache just after veejay-server installation (ex `sudo ldconfig` or similar)

If you want veejay to be optimized for the current cpu-type, you do not need to pass any parameters. If you do not know what cpu veejay will be running on , pass `--with-arch-target=auto` to configure.

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

There are several [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs) available for veejay.

Also, Veejay looks in a few common locations to find plugins. You can even list
more locations in `$HOME/.veejay/plugins.cfg`

You can change the default FX parameter values by editing the files in
`$HOME/.veejay/frei0r/` and `$HOME/.veejay/livido/`

For more verbose information about plugins and FX check [How to Plugins](./HowtoPlugins.md)

## Debugging

if you want to debug veejay-server (or if you want to submit a meaningful backtrace), build with:

     ./configure --enable-debug

see also : [How to debug](./HowToDebugging.txt)
