                                                                                                              
        //    ) )                                             //   ) )                                        
       //    / /                ( )      ___       / ___     ((         __  ___     ___        __     __  ___ 
      //    / /     //   / /   / /     //   ) )   //\ \        \\        / /      //   ) )   //  ) )   / /    
     //  \ \ /     //   / /   / /     //         //  \ \         ) )    / /      //   / /   //        / /     
    ((____\ \     ((___( (   / /     ((____     //    \ \ ((___ / /    / /      ((___( (   //        / /      


_It is advised to read the howto and the MAN page as well._

### Launch and select source
You can launch veejay with

    $ veejay -d

This should show some moving black/white footage. Most of the effects will be boring on this footage,
so you can try to open your video4linux device with

    $ sayVIMS -m "240:0 1;" (device 0, channel 1)

or your dv1394 firewire device.

    $ sayVIMS -h localhost -p 3490 -m "241:63;"


To use mplayer, create a FIFO first:

    $ mkfifo stream.yuv

Use something like `mplayer -vo yuv4mpeg -x 352 -y 288 -vf scale -zoom`

and open the stream in veejay's console:

    $ sayVIMS -m "243:stream.yuv;"

Move your mouse pointer to the SDL window (so it dissapears) and press 'ESC' to
switch from the dummy footage to the last created or played video stream. 

Try loading an AVI file with something like:  
_(mjpeg-video-file.avi __must be__ encoded for veejay, see [codec](./README.video-codec.md)_

    $ veejay -v -g mjpeg-video-file.avi

The `-v` commandline option generates extra debugging output.  
The `-g` load clips has samples.

By default, veejay uses a SDL window for displaying video. You can specify veejay to
write to STDOUT :

    $ veejay -O3 -o stdout mjpeg-video-file.avi | yuvplay

__In this mode__, the console input and SDL keyboard functions __are disabled__. You must use
the `sayVIMS` commandline utility to interact with veejay or with an alternative utility like sendOSC. Refer to the howto [VIMS](./VIMS.md) for more information.

Let's discover with some SDL keyboard functions :

### Sampling and select sample
Once you have loaded veejay, preferably with a videofile :

* press [KP 1] , [left bracket], [KP 3], [right bracket] , [F1]

This will create a virtual clip (in memory) from your entire video file.

* If you press [KP divide] , veejay will return to plain video mode so you can create more clips.
* If you press [ESC] , veejay will switch from playing streams to playing clips or vice versa

* Press [F1] to [F12] to select a clip,
* Press [SHIFT] + [t] to toggle transitionning between clips,
* Press [1] to [9] to quick jump into clip position, 10%, 20%, 30% ect.
* Press [SHIFT] + [1] to [9] to select a clip bank (1 = clips 1 to 12,  2 = clips 12 to 24, etc )

### Add an effect
Once you are playing a clip/stream, simply press

* [Cursor UP] , [ENTER]

To add an effect in the current effect chain entry.

If you add a video effect, try pressing [-] and [=] to select another channel and [/] to toggle between clip/stream sources


### Effect chain
Veejay supports chaining of effects since day 0, a number of keys have some importance :

* [-] ,[=] and [/] , select previous / next channel and toggle sources
* [END] enable / disable the chain
* [KP -] select the previous entry
* [KP +] select the next entry
* [ALT]+[END] enable / disable the current selected entry
* [ENTER] add an effect from the list to the chain
* [DEL] remove an effect from the chain

### More keys ?
Also, you can press [HOME] to see clip or stream information.

Try the keys [A] to [L] to increase/decrease playback speed.  
Try the keys combinaison [ALT] + [A] to [L] to increase/decrease frame repetition.

_See __man veejay__ for an overview of both console input and SDL keyboard events_

### Load custom chain
Also, you can load some predefined custom effect chain templates that will put a
template on your effect chain when you press SHIFT + some alphabetic character.

First, load an action file

    $ veejay -l test/livecinema/action-file.xml

Or in veejay's console

> al test/livecinema/action-file.xml

(activate a stream or clip and) press [SHIFT]+[S] or [SHIFT]+[B] or [SHIFT]+ ...


 Enjoy!
