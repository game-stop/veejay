
     __   __   ______  ______  __     __   ______   ______   __  __
    /\ "-.\ \ /\  ___\/\__  _\/\ \  _ \ \ /\  __ \ /\  == \ /\ \/ /
    \ \ \-.  \\ \  __\\/_/\ \/\ \ \/ ".\ \\ \ \/\ \\ \  __< \ \  _"-.
     \ \_\\"\_\\ \_____\ \ \_\ \ \__/".~\_\\ \_____\\ \_\ \_\\ \_\ \_\
      \/_/ \/_/ \/_____/  \/_/  \/_/   \/_/ \/_____/ \/_/ /_/ \/_/\/_/
                                                            and veejay

_We are entering in some advanced veejay's features, it is not necessary to read / understand those aspects for basics usages._

We will devellop some advanced veejay's network features like, streaming (point to point), multicasting (point to multipoint)...
Have a look at the end of this documentation how to setup multicast on your machine.

Also, you should read [VIMS](./VIMS.md) documentation before going further.

# Veejay stats

None.

# Stream over the network - unicast

    TCP socket for sending commands    : + 0 (sayVIMS -h)
    TCP socket for receiving status    : + 1 (reloaded)
    TCP socket for querying commands   : + 2 (reloaded - editlist/sampellist etc)
    UDP multicast frame sender         : + 3 (input stream multicast/other applications)
    UDP multicast for sending commands : + 4 (sayVIMS -g)

## How to customize:

1. Starts "veejay 1" on default port `3490`

    `$ veejay movie1.avi`

    If there is already a veejay running, the next instance will start at `DEFAULT_PORT + 1000`

2. Open another terminal, with `-p` you can give "veejay 2" port offset :

    `$ veejay movie2.avi -p 5000`

3. Let's "veejay 2" make a unicast connection with "veejay 1" (in another term)

    `$ sayVIMS -m "245:3490 localhost;" -p 5000 -h localhost`

4. Switch to "veejay 2" video window and press [Echap]

    If everything is OK, the video `movie1.avi` will appear in "veejay 2". This
    means that "veejay 2" now is connected with "veejay 1" mirroring it!

5. Stop "veejay 1" [CTRL]+[c] or VIMS 600 !

6. "veejay 2" still display the last frame, press [Keypad /] to come back to plain mode

7. Restart "veejay 1"

8. Press [ESC] in "veejay 2" to resume streaming (auto-reconnect)

# Veejay can stream over the Network (UDP / Multicast )

## How to activate:

1. `$ veejay -T 224.0.0.50 -p 5000 -v movie1.avi`
2. `$ veejay -d -W <movie1 width> -H <movie1 height>`

3. `$ echo "246:5000 224.0.0.50;" |sayVIMS`

4. Press [F2] to activate newest created stream

# Veejay can receive OSC (Open Sound Control) messages over Multicast protocol

## How to activate:

1. `$ veejay --multicast-osc 224.0.0.30 movie1.avi`

    Note that you must have 'multicast' enabled in your kernel configuration.
Use the tool `mcastOSC` in `test/OSC` to send OSC messages to all
veejay's you have started with `--multicast-osc` (on multiple hosts!!)

2. `$ export MCAST_ADDR=224.0.0.30`
4. `$ mcastOSC -r 3495`

# Veejay can send/receive VIMS packets over Multicast protocol

## How to activate:

1. `$ veejay --multicast-vims 224.0.0.32 movie1.avi`
2. `$ veejay --multicast-vims 224.0.0.32 movie2.avi`

3. `$ sayVIMS -g 224.0.0.32 -f test/vims/magicmirror-vims.txt`

# How to setup multicast?

## How to setup:
(example to setup multicast over loopback)

1. Turn off firewall (or add allow rule)

2. `# /sbin/ifconfig lo multicast`  
or if using systemd  
` # /sbin/ip link set lo multicast on`  
` # /sbin/ip link set lo up`

3. `# route add -net 224.0.0.0 netmask 240.0.0.0 dev lo`  
or if using systemd  
`# /sbin/ip route add 224.0.0.0/4 dev lo`

4. `# echo 0 > /proc/sys/net/ipv4/icmp_echo_ignore_broadcasts`

5. `# echo 1 > /proc/sys/net/ipv4/ip_forward`

6. `$ ping 224.0.0.1`

    Should reply !

You can replace `lo` for the interface you want to use.

If you have multiple interfaces, you must set `VEEJAY_MCAST_INTERFACE` to the
interface you want to use for multicasting:

`$ export VEEJAY_MCAST_INTERFACE="lo"`

# But ... what is multicast?

Multicasts enables you to (ab)use the network topology to transmit
packets to multiple hosts.

See : [Wikipedia / Multicast](https://en.wikipedia.org/wiki/Multicast)

## Limitations

1. Audio is not supported
2. Frame data is only compressed with LZO, bandwith consumption for 720p is ~
   40 MB/sec

## What header format?

unicast/multcast packet format:

[field:byte size]

Header is the first 44 bytes:

     [width:4][height:4][pixel_format:4][compressed_length:8]
     [plane0 length:8][plane1 length:8][plane2 length:8]
     [DATA:compressed_length]

Data contains redundant information:

     [DATA  [ Y compressed length: 4][ U compressed length: 4 ]
            [ V compressed length: 4][ reserved: 4 ]
            [ pixel_data: Y + U + V compressed length ]  
     ]

Compressed length should match 16 + Y + U + V compressed length.
If compressed length is zero, then the frame is not compressed and the plane
lengths are the sizes of the YUV planes.

For multicast, the whole of this blob is send in chunks of 1500 bytes.

Each chunk starts with the following header:

     [sequence number:4][usec:8][timeout:4][total packets:4][ data: 1480 bytes ]

The last chunk is zero padded to fill the 1500 byte marker

