Memory concern with veejay
===============

Dynamic or Static FX chain
-----

Veejay pre-allocates a large buffer (aprox. 180 MB for 1280x720 video) to cache
the entire fx_chain.

If you wish to turn off this option, you can use the `-M` or `--dynamic-fx-chain`
commandline parameters.

When dynamic allocation is enabled, each chain entry is allocated and freed on
the fly between switching samples.  
Individual FX are always allocated and freed between switching samples.

```
-M / --dynamic-fx-chain     Do not keep FX chain buffers in RAM (default off)
```

Caching video frames
--------

Veejay can (optionally) use your RAM to cache video frames from file to memory.
By default this option is turned off.

You can enable it by specifying both the `-m` and `-l` commandline parameters.
They both expect arguments.  
Enter a percentage of your total physical RAM memory to use for `-m` and divide
it equally over `-j` samples.

These options will allow you to commit the frames from the sample's EDL to
memory while playing, so that subsequent loops no longer require disk-access.  
This option should help you increase the audio playback quality when using the
FX chain (reducing or elminating stutter)

The cache size is limited to the amount of RAM available in your machine. If the
cache is full, Veejay must decide which frames to discard. Veejay will discard
the frame furthest away from the current position.

You can configure the cache with two commandline options:

```
-m / --memory     <num>     Maximum memory to use for cache (0=disable, default=0 max=100)
-j / --max_cache  <num>     Divide cache memory over N samples (default=0)
```

The second option, `-j` is used to divide up the cache memory into a number of
equal sized chunks. Veejay will cache by default up to 8 samples into your
system's main memory, if you specified the `-m` option.
