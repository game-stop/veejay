Veejay has a very simple keyboard layout

On startup, veejay loads $HOME/.veejay/keyboard.cfg after initializing its
default keybindings. This allows you to overwrite or add keybindings.


Message format:

<VIMS_ID>,<SDL Key>,<Modifier>,"<VIMS message body>"

Modifier: None=0, ALT=1, CTRL=2, SHIFT=3
SDL Key: See /usr/include/SDL/SDL_keysym.h
VIMS_ID: See veejay/vims.h
VIMS message body: quoted VIMS message body


Example file:

The file below maps the VIMS_PROJ_INC event to the LEFT,RIGHT,UP,DOWN keys

"$ veejay -u" lists the format of VIMS_PROJ_INC:

	  VIMS selector 160    'Increase projection/camera point'
      FORMAT: '%d %d', where:
              Argument 0 is X increment
              Argument 1 is Y increment


The following assigns the LEFT,RIGHT,UP,DOWN keys to VIMS_PROJ_INC:

160,276,2,"-1 0"
160,275,2,"1 0"
160,273,2,"0 -1"
160,274,2,"0 1"


All lines in the file must be conform this format
See keyboard.cfg





