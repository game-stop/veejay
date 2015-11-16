```
 __   __   __     _____     ______     ______     __     __     ______     __         __        
/\ \ / /  /\ \   /\  __-.  /\  ___\   /\  __ \   /\ \  _ \ \   /\  __ \   /\ \       /\ \       
\ \ \'/   \ \ \  \ \ \/\ \ \ \  __\   \ \ \/\ \  \ \ \/ ".\ \  \ \  __ \  \ \ \____  \ \ \____  
 \ \__|    \ \_\  \ \____-  \ \_____\  \ \_____\  \ \__/".~\_\  \ \_\ \_\  \ \_____\  \ \_____\ 
  \/_/      \/_/   \/____/   \/_____/   \/_____/   \/_/   \/_/   \/_/\/_/   \/_____/   \/_____/ 
```

Videowall in veejay

Requires:  lvdshared from pluginpack (https://github.com/c0ntrol/veejay/tree/master/veejay-current/plugin-packs/lvdshared)

You can setup a video wall by starting multiple veejays. One of the veejays must be the master veejay, responsible for deciding which veejay renders what part of the image. Only the master requires a special command-line option and a file to read its configuration from. The other veejays can be started normally. 

First, to setup a video wall you need to write a configuration file that lists the veejay connections:

    screen=2x3
    row=0 col=0 port=4490 hostname=localhost
    row=0 col=1 port=5490 hostname=localhost
    row=0 col=2 port=6490 hostname=localhost
    row=1 col=0 port=7490 hostname=localhost
    row=1 col=1 port=8490 hostname=localhost
    row=1 col=2 port=9490 hostname=localhost

Then, you continue starting 6 more veejays. If you start them on other machines, change the hostname resp. or enter an ip address.

If the other veejay is running locally, veejay will share its memory otherwise the image part is send through the network.

Finally, you start the master veejay by invoking the `--split-screen` option.

`veejay --split-screen -a0 -Z6`


Examples:
(test/videowall/videowall.sh)

    #!/bin/bash
    # split screen configuration file is in $HOME/.veejay/splitscreen.cfg
    
    # write new config file!
    cat << EOF > $HOME/.veejay/splitscreen.cfg
    screen=2x3
    row=0 col=0 port=4490 hostname=localhost
    row=0 col=1 port=5490 hostname=localhost
    row=0 col=2 port=6490 hostname=localhost
    row=1 col=0 port=7490 hostname=localhost
    row=1 col=1 port=8490 hostname=localhost
    row=1 col=2 port=9490 hostname=localhost
    EOF
    # end of writing new config file

    # start the individual screens

    veejay -d -a0 -w 240 -h 288 -D -p4490 &
    veejay -d -a0 -w 240 -h 288 -D -p5490 &
    veejay -d -a0 -w 240 -h 288 -D -p6490 &
    veejay -d -a0 -w 240 -h 288 -D -p7490 &
    veejay -d -a0 -w 240 -h 288 -D -p8490 &
    veejay -d -a0 -w 240 -h 288 -D -p9490 &


    sleep 5

    # start the master screen

    veejay --split-screen -d -a0 -Z6 -w 720 -h 576

