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
