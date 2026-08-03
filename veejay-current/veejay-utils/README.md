# veejay-utils

Small command-line utilities for working with VeeJay.

## sayVIMS

`sendVIMS` / `sayVIMS` is a command-line client for sending VIMS commands to a
running VeeJay instance.

Current version:

```text
veejay sayVIMS 1.2.0
```

## Usage

```sh
sayVIMS [options] [messages]
```

### Options

| Option | Description |
| --- | --- |
| `-p` | VeeJay port. Default: `3490` |
| `-g` | VeeJay multicast group. Default: `224.0.0.31` |
| `-h` | VeeJay hostname. Default: `localhost` |
| `-m` | Send a single message |
| `-i` | Interactive mode |
| `-f file` | Read commands from a file or special file |
| `-d` | Dump status to stdout |
| `-b` | Base64-encode binary data |
| `-v` | Verbose output |
| `-?` | Print help |

VIMS commands passed on the command line should be wrapped in quotes.

Multiple messages may be supplied separated by whitespace.

VIMS reply messages are displayed only in interactive mode.

Exit interactive mode by typing:

```text
quit
```

## Examples

Quit a running VeeJay instance:

```sh
sayVIMS "600:;"
```

The same command can be piped through stdin:

```sh
echo "600:;" | sayVIMS
```

Send a command to a remote VeeJay instance:

```sh
sayVIMS -h 192.168.100.12 -m "600:;"
```

Add effect `101` (`Mirror`) to an effect-chain entry:

```sh
sayVIMS -m "360:0 0 101 1;"
```

## Interactive mode

For commands that return VIMS replies, use interactive mode:

```sh
sayVIMS -i
```

This keeps the connection open so commands can be entered repeatedly and
responses printed as they arrive.

## About VIMS

VIMS is VeeJay's control protocol. It is used to control playback, samples,
streams, effects, effect chains, recording, sequencing, audio, and other
runtime features of the VeeJay server.

For the available command IDs and their arguments, refer to the VIMS
definitions shipped with the matching VeeJay server version.
