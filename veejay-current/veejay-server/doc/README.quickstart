
It is advised to read the howto and the MAN page as well. 

You can launch veejay with

$ veejay -d

This should show some moving black/white footage. Most of the effects will be boring on this footage,
so you can try to open your video4linux device with

sayVIMS "240:0 1;" (device 0, channel 1)

Or, to use mplayer, create a FIFO first:

$ mkfifo stream.yuv

Use something like 'mplayer -vo yuv4mpeg -x 352 -y 288 -vf scale -zoom' 

and open the stream in veejay's console:

sayVIMS "243:stream.yuv;"

Move your mouse pointer to the SDL window (so it dissapears) and press 'ESC' to
switch from the dummy footage to the last created or played video stream. 

Try loading an AVI file with something like:

$ veejay -v mjpeg-video-file.avi

The '-v' commandline option generates extra debugging output.

By default, veejay uses a SDL window for displaying video. You can specify veejay to
write to STDOUT :

$ veejay -O3 -o stdout mjpeg-video-file.avi | yuvplay

In this mode, the console input and SDL keyboard functions are disabled. You must use
the sendVIMS commandline utility to interact with veejay or with an alternative utility like sendOSC.
Refer to the howto for more information.


Once you have loaded veejay (preferably with a videofile)

  (see man veejay for an overview of both console input and SDL keyboard events)

   press 'KP 1' , 'left bracket', 'KP 3', 'right bracket' , 'F1'

   This will create a virtual clip (in memory) from your entire video file.

   If you press 'KP divide' , veejay will return to plain video mode so you can create more clips.
   If you press 'ESC' , veejay will switch from playing streams to playing clips or vice versa

   Press F1 to F12 to select a clip,
   press 1 to 9 to select a bank (1 = clips 1 to 12,  2 = clips 12 to 24, etc )

   Once you are playing a clip/stream, simply press
     'Cursor UP' , 'ENTER'

   If you add a video effect, try pressing '-' and '=' to select another channel and '/' to
   toggle between clip/stream sources


   Veejay supports chaining of effects since day 0, a number of keys have some importance

	'-' ,'=' and '/'

	'END' for enabling/disabling the chain
	'KP -' for selecting the previous entry
	'KP +' for selecting the next entry
        'ALT+END' for enabling/disabling the current selected entry
	'ENTER' for adding an effect from the list to the chain
	'DEL'  for removing an effect from the chain
	
 
   Also, you can press 'HOME' to see clip or stream information.

   Try the keys 'A' to 'L' to increase/decrease playback speed.


Also, you can load some predefined custom effect chain templates that
will put a template on your effect chain when you press SHIFT + some alphabetic character 

First, load an action file

$ veejay -l test/livecinema/action-file.xml

Or in veejay's console

> al test/livecinema/action-file.xml

(activate a stream or clip and) press SHIFT+S or SHIFT+B or SHIFT+ ... 


Except all that, try this:


sayVIMS -h localhost -p 3490 "241:63;"
(press F7)

to open your dv1394 firewire device

or
sayVIMS -h localhost -p 3490 "240:0 1;"
(press F7)

to open your video4linux device /dev/video0, channel 1



 Enjoy!
