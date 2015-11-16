
+[//]: # ( comment : installation section duplicated from /README.md)

## Installation

Veejay is divided into multiple packages:

1. [veejay-server](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-server)
1. [veejay-client](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-client)
1. [veejay-utils](https://github.com/c0ntrol/veejay/tree/master/veejay-current/veejay-utils)
1. [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs)

For each package, run confgure and make:


```bash
 ./autogen.sh
 ./configure
 make && make install
```

If you want veejay to be optimized for the current cpu-type, you do not need to pass any parameters. If you don't now what cpu veejay will be running on , pass `--with-arch-target=auto` to configure.


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
