



    10x0            0x     x10x0x10x0x10x   10x0x10x0x10x0        x0x1         x0x1       10x0        x0x1
  0x10x0            0x0x   x10x0x10x0x10x   10x0x10x0x10x0        x0x1       10x0x10x      10x0x1    10x0x1
0x0x10              0x0x10 x10x             10x0                  x0x1       10x0x10x       x0x10x  10x0
0x0x10x0            0x0x10 x10x0x10x0x1	    10x0x10x0x10          x0x1     0x10    0x0        x10x0x10
  0x10x0          x10x0x   x10x0x10x0x10x   10x0x10x0x10          x0x1     0x10x0x10x0        x10x0x10
    10x0x1      x0x10x     x10x             10x0                  x0x1     0x10x0x10x0          0x0x
      x0x10x0x10x0x1       x10x             10x0            x10x   x0x1    0x10x0x10x0x10       0x0x
        x10x0x10x0         x10x0x10x0x10x   10x0x10x0x10x0  x10x  10x0    0x0x        0x10      0x0x
          0x0x10           x10x0x10x0x10x   10x0x10x0x10x0    0x010x0     0x0x        0x10      0x0x    


 			 :: Veejay Utilities, a collection of commandline programs for veejay ::
	                  	               http://veejayhq.net


# Eidolon

**Eidolon** is an evolving Auto-VJ organism for **VeeJay**.

It connects to a running VeeJay instance over VIMS, builds FX chains, mutates them over time, reacts to beat/tempo controls, learns from user feedback, and keeps itself inside the realtime budget by watching VeeJay's measured FPS status tokens.

It is not a preset randomizer. Eidolon has a small internal population of organisms, chain grammar, mutation, memory, immune rejection, and realtime metabolism.

```text
EIDOLON wakes up.
It grows an FX body.
You teach it by keeping, liking, hating, or clearing what it creates.
It tries to survive at realtime speed.
```

## First contact

Start VeeJay first, then run Eidolon:

```sh
./eidolon
```

On startup, Eidolon prints a small boot banner and drops into its terminal prompt:

```text
eidolon://0/live>
```

Try this first:

```text
mutate
```

The screen should change immediately. Then inspect what Eidolon built:

```text
chain
```

If you like what you see:

```text
like
```

If it is excellent:

```text
love
```

If it is bad:

```text
hate
```

In normal VeeJay use, simply clearing the FX chain is also treated as rejection when realtime clear-learning is enabled.

To see the compact runtime summary:

```text
status
```

For the full field manual:

```sh
man eidolon
```

## What Eidolon does

Eidolon controls a VeeJay FX chain as if it were a living body.

- An **organism** is one candidate FX chain.
- A **gene** is an FX entry.
- A **cell** is a parameter with motion, heat, target, velocity, and life state.
- A **population** contains multiple organisms that compete, mutate, and survive.
- The **brain** learns which FX tend to work from status-token context and user feedback.
- The **immune system** remembers rejected FX and rejected adjacent FX pairs.
- The **metabolism** watches realtime FPS and shrinks or grows the chain accordingly.

At 25 FPS, VeeJay has about **40 ms per frame**. Eidolon uses the backend `real_fps` and target FPS status tokens to estimate whether its current chain is too heavy.

If the chain is too expensive, Eidolon can reduce the number of active FX. In survival mode it may even enter a clean **0-FX hibernation state**, then wake and grow again when the realtime budget recovers.

## Building

From a configured VeeJay source tree, Eidolon is built like a normal autotools component:

```sh
./autogen.sh
./configure
make
```

During development, a direct build from the source tree can look like:

```sh
gcc -DHAVE_CONFIG_H -I. -I./veejay-current/veejay-server \
    -o eidolon src/eidolon_life.c -lveejaycore -lm
```

The exact include paths depend on where the VeeJay tree is checked out and configured.

## Running

Default local connection:

```sh
./eidolon
```

Common options:

```text
-h, --help      show command-line help
-v             enable verbose automatic logs
-q             quiet startup / no interactive banner
-T             accepted for compatibility; Eidolon is terminal-only
```

Eidolon is intentionally quiet by default. It gives one response per command. Automatic mutation, realtime, and chain events do not flood the terminal unless verbose mode is enabled.

Inside the shell:

```text
verbose on
verbose off
```

## Useful commands

### Inspect

```text
status       one-line organism summary
chain        current FX chain entries
brain        learner state and FX memory
roles        chain grammar/category table
rt status    realtime governor state
help         command overview
intro        first-contact guide
banner       replay boot glyph
```

### Teach

```text
like         reward current organism
love         strongly reward current organism
hate         reject and mutate away
kill         hard rejection; strong immune memory
```

### Shape the organism

```text
mutate       soft mutation now
hard         hard mutation now
rebuild      rebuild current organism
chaos N      0..1, higher means more violent motion
scene N      scene lifetime in ticks
life SEC     FX lifetime in seconds
```

### Chain size

```text
minchain N       minimum FX entries Eidolon controls
maxchain N       maximum FX entries Eidolon controls
fxcount N        exact FX count
fxrange A B      controlled FX range
morefx [N]       raise minimum controlled FX count
lessfx [N]       lower minimum controlled FX count
fullchain        force 19 FX entries
```

`0` is valid and means clear/off. In adaptive realtime mode, 0-FX survival is allowed.

### Realtime governor

```text
rt status
rt auto
rt off
rt lock
rt unlock
rt target RATIO
rt clearlearn strict
rt clearlearn selected
rt clearlearn off
```

Default clear-learning is strict. Eidolon avoids treating a merely empty selected FX entry as proof that the whole chain was cleared.

### Beat / Auto-FX

```text
amount N       beat auto-fx amount, 0..100
beatmode N     0 off, 1 primary, 2 primary+motion, 3 +memory, 4 chaos
action N       0 none, 2 auto-fx, 3 breakbeat+auto-fx, 4 breakbeat
```

### Exploration

```text
explore off|[SEC [LEN [N]]]
combos [SEC [LEN [N]]]
try19 [SEC [N]]
```

These commands are useful for auditioning many organisms quickly.

### Source / routing

```text
sample                    create and play a new random sample
samples off|on FRAMES      disable/enable sample creation
mix sample|stream ID       source/channel for extra-frame mixer FX
push [next|+N|N|PORT|HOST:PORT] [sample ID]
```

### State

```text
save [file]
load [file]
```

Default state file:

```text
autovj.life
```

The state file stores the fuzzy mind, learned FX preferences, cost pressure, immune memory, and population state.

## Runtime files

Generated runtime files should not be committed:

```text
autovj.life
*.life
sample_*.edl
```

## Signals

`SIGUSR1` saves the current Eidolon mind.

Example:

```sh
kill -USR1 $(pidof eidolon)
```

## Terminal behaviour

Eidolon is terminal-only. There is no split terminal UI and no raw mouse mode.

Colors are used as terminal affordances:

- cyan for identity, prompt, and headings
- green for healthy/active states
- yellow for warnings and manual/realtime bounds
- red for unhealthy/rejected states
- dim text for secondary hints

Colors are disabled when `NO_COLOR` is set or when `TERM=dumb`.

## Design notes

Eidolon should be allowed to surprise the user, but it should not abuse the live session.

Its priorities are:

1. Stay realtime.
2. Keep the chain musically and visually alive.
3. Learn from user rejection and approval.
4. Avoid repeating toxic FX combinations.
5. Mutate enough to stay interesting.

## License

GPL-2.0-or-later, matching VeeJay tooling.
