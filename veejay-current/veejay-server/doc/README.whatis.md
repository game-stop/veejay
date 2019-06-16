```
      ___           ___           ___            ___         ___           ___     
     /\__\         /\  \         /\  \          /\  \       /\  \         |\__\    
    /:/  /        /::\  \       /::\  \         \:\  \     /::\  \        |:|  |   
   /:/  /        /:/\:\  \     /:/\:\  \    ___ /::\__\   /:/\:\  \       |:|  |   
  /:/__/  ___   /::\~\:\  \   /::\~\:\  \  /\  /:/\/__/  /::\~\:\  \      |:|__|__ 
  |:|  | /\__\ /:/\:\ \:\__\ /:/\:\ \:\__\ \:\/:/  /    /:/\:\ \:\__\     /::::\__\
  |:|  |/:/  / \:\~\:\ \/__/ \:\~\:\ \/__/  \::/  /     \/__\:\/:/  /    /:/~~/~   
  |:|__/:/  /   \:\ \:\__\    \:\ \:\__\     \/__/           \::/  /    /:/  /     
   \::::/__/     \:\ \/__/     \:\ \/__/                     /:/  /     \/__/      
    ~~~~          \:\__\        \:\__\                      /:/  /                 
                   \/__/         \/__/                      \/__/                  
```

a **visual instrument** and **realtime video sampler**
==============

_____video sampling, live streaming, direct editing...._____

Veejay is a live performance tool featuring simple non-linear editing and mixing from multiple sources (files,
devices, streams...). You can load multiple video clips, cut and paste portions of video/audio and save it as an
EditList. Also, you can record new clips from existing clips or (live) streams. With these clips you can change
playback speed (slow motion/acceleration), change the loop type and set markers.

_____visuals effects, blending methods, alpha transitions..._____

With both clips and streams you can edit the effect chain and mix from multiple sources to one. Veejay has a 160+
effects, divided into three categories: Image, Video and Alpha Effects (only with Images Effects you can not select
a channel to mix up).

Veejay has many frame blending methods, some of these are: Additive, Substractive, Difference Negate, Relative Addition
and Selective Replacement. Next to blending, you can key on Luma and Chroma separately or combined or simply use
Transitions or other effects. Alpha channel is combined with FX that can deal with. Some effects have a mode
parameter "Alpha" that functions like an on/off switch but others require an alpha channel to work.

_____keyboard interface, midi/osc controlers, vims internals, automation..._____

Most edit and navigation commands are mapped to single key press commands, this allows you to control, depending on
the playback mode, video navigation, the effect chain, effect parameters and clip properties at playback time. You can assign any of those commands to a MIDI controller. 

Veejay can be remotely controlled through using OSC (Open Sound Control) or via its own internal message interface
'VIMS'. 'VIMS' allows you to create/load/save effect chain templates and to add customized events which can be
triggered by a keypress or a remote message. You can record sequences of commands for playback automation.

_____files, cameras, streams, pipe, fake devices..._____

Also, you can record a new clip on the fly from a live feed or from the video clip you are playing. If requested, the
recorded video file will be added to the edit descision list and activated as a new video clip. This is particular
usefull for time-looping, rebouncing and rough clip scratching/editing.

Veejay supports streaming from multiple video sources to one, this can be a Video4Linux device, a vloopback device or
a yuv4mpeg stream. You can chain several veejays with effectv and vice versa to create some amazing footage.

**veejay is licensed as Free Software (GNU).**

[http://veejayhq.net](http://veejayhq.net)
