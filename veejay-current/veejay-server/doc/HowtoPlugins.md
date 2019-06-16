Video Plugins FX
========

Veejay contain more than 160 built-in FX, many unique and original FX filters.  
But you can have more !

Plugin-packs available with veejay
-----------------
There are several [plugin-packs](https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs) available with veejay:

* **lvdcrop** ; a couple of crop filters and a port of frei0r's scale0tilt
* **lvdshared** ; a couple of plugins that implement a producer/consumer mechanism for shared video resources
* **lvdgmic** ; a couple of [GMIC](https://gmic.eu/) based filters, although slow in processing they are quite amazing

To compile and install a plugin-pack:
```bash
$ cd veejay-source/plugin-packs/lvdgmic
$ ./autogen.sh
$ ./configure && make
```

How veejay will find installed plugins ?
--------------------
By default, veejay looks in the following commmon locations to find plugins:
* /usr/local/lib
* /usr/lib/
* /usr/local/lib64
* /usr/lib64
* /usr/local/lib/frei0r-1
* /usr/lib/frei0r-1
* /usr/lib64/frei0r-1

You can list more locations in `$HOME/.veejay/plugins.cfg`  
Create a file to tell veejay where to find plugins:
```
$ mkdir ~/.veejay
$ vi ~/.veejay/plugins.cfg
```

Apart from the differences due to your OS, the contents of the file can look like:
```
/usr/local/lib/freeframe
```

Veejay will pick up the plugins configured in the `plugins.cfg` file the next time you start it.

Plugins default values
------------------
You can change the default parameter values by editing the files in
`$HOME/.veejay/frei0r/` and `$HOME/.veejay/livido/`
