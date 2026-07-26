![VeeJay banner](https://veejayhq.github.io/img/header.png)

# VeeJay

**VeeJay is a real-time visual instrument, video sampler, and live-performance engine for Linux.**

It is designed for playing video rather than merely playing back a file: create samples while the engine is running, change speed and direction, loop and scratch material, build live FX chains, automate VIMS commands, record the result, and control several VeeJay instances as one performance system.

The backend performs frame-oriented video processing in planar YUV and exposes its controls through **VIMS**. **Reloaded**, the GTK3 client, is status-driven: the running backend remains authoritative while the user interface provides visual editing, performance controls, previews, automation, and monitoring.

## Project components

| Component | Purpose |
| --- | --- |
| [`veejay-core`](./veejay-core) | Shared memory, networking, frame, audio, protocol, and utility libraries used by the other components. |
| [`veejay-server`](./veejay-server) | The `veejay` real-time video engine, sampler, FX host, recorder, stream processor, and VIMS server. |
| [`veejay-client`](./veejay-client) | **Reloaded**, the GTK3 graphical client for live control and editing. |
| [`veejay-utils`](./veejay-utils) | Command-line tools, including `sayVIMS`, for scripting and direct VIMS interaction. |
| [`veejay-eidolon`](./veejay-eidolon) | **Eidolon**, an experimental apprentice Auto-VJ that builds and mutates FX chains while observing beat, performance, feedback, and the real-time frame budget. |
| [`sendVIMS`](./sendVIMS) | A Pure Data external for sending event-style VIMS commands and receiving backend status. |
| [`plugin-packs`](./plugin-packs) | Optional LiViDO/GMIC/crop/shared-resource/ASCII-art plugin collections. |


## Feature overview

### Live performance

- Soft real-time, frame-oriented video processing in planar YUV.
- Plain EDL, Sample, Stream, and Pattern playback modes.
- Frame-accurate seeking, markers, looping, reverse playback, variable speed, slow motion, scratching, and dynamic frame rate.
- Live sampling and recording of processed video.
- Multiple VeeJay instances can be connected and operated as one performance system.

### Editing and sequencing

- Non-destructive Edit List editing with cut, copy, paste, delete, crop, range movement, segment trimming, snapping, separators, and named regions.
- Searchable Backend Media browser with drag-and-drop loading.
- Sample and stream banks with previews and live status.
- Four-bank Sequence Editor with 120 slots per bank.
- Eight-track Pattern Editor for frame-accurate VIMS automation, loops, command capture, block editing, undo/redo, and drag-and-drop from VIMS History.
- MultiTrack Edit with aligned instance timelines, independent previews, transport control, buffered-stream display, A/B switching, CUT, Dissolve, Shape Wipe, and Drift Lock.

### Effects and automation

- Large collection of native real-time video effects, plus optional Frei0r, LiViDO, GMIC, and other plugin packs.
- Multi-entry FX chains with two-source effects, alpha compositing, masks, transitions, entry movement, replacement, and chain opacity.
- Parameter animation using keyframes, linear and spline curves, freehand drawing, generated shapes, random motion, and noise.
- Beat-aware Auto FX and Break Beat control.

### Audio

- JACK-compatible audio output and external audio input.
- Original, external JACK, WAV, and silent audio sources.
- Separate controls for JACK volume, sample audio, audio mixing, recording source, beat analysis, and synchronisation.
- Beat detection, clean monitoring, trick-play monitoring, tempo following, tempo bridging, and track alignment.

### Control and networking

- VIMS control over TCP/IP with status-driven clients.
- GTK3 Reloaded client, `sayVIMS` command-line tools, `sendVIMS` for Pure Data, MIDI learning, keyboard control, scripts, and Eidolon Auto-VJ experiments.
- Multi-instance control, VIMS forwarding, unicast and multicast streaming, and VeeJay chaining.

### Output

Depending on the configured build, VeeJay supports SDL2 windowed or full-screen output, headless operation, preview and image retrieval, YUV4MPEG streaming, V4L2 loopback, and network video output.

## Building from source

VeeJay is split into independent autotools projects and must be built in dependency order.

### Typical Debian/Ubuntu build dependencies

The exact package set depends on enabled outputs and optional plugins. A useful base installation is:


```bash
sudo apt-get update
sudo apt-get install \
    build-essential autoconf automake libtool m4 pkg-config \
    libglib2.0-dev \
    libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev libswresample-dev \
    libgtk-3-dev libgdk-pixbuf-2.0-dev \
    libsdl2-dev libjack-jackd2-dev libasound2-dev \
    libxml2-dev \
    libx11-dev libxext-dev libxinerama-dev \
    libjpeg-dev liblo-dev \
    libfreetype6-dev libfontconfig1-dev libunwind-dev \
    linux-libc-dev
```

Optional legacy DV and QuickTime support can be enabled where these packages are available:

```bash
sudo apt-get install libdv4-dev libquicktime-dev
```

Individual plugin packs may require additional libraries. Their `./configure` scripts report any pack-specific dependency that is still missing.

### Build order

For the autotools components:

```bash
for project in \
    veejay-core \
    veejay-server \
    veejay-client \
    veejay-utils \
    veejay-eidolon
do
    (
        cd "$project"
        ./autogen.sh
        ./configure
        make -j"$(nproc)"
        sudo make install
    )
done
```

Refresh the shared-library cache after installing the core libraries when required by the system:

```bash
sudo ldconfig
```

`sendVIMS` uses its own Makefile:

```bash
cd sendVIMS
make -j"$(nproc)"
sudo make install
```

Each plugin pack is built separately:

```bash
for pack in lvdasciiart lvdcrop lvdgmic lvdshared
do
    (
        cd "plugin-packs/$pack"
        ./autogen.sh
        ./configure
        make -j"$(nproc)"
        sudo make install
    )
done
```

### Fonts

Reloaded and the server OSD use TrueType rendering. If the installed data set does not provide suitable fonts, add or link fonts under:

```text
$HOME/.veejay/fonts
```

## Quick start

Start a server with a clip loaded as a sample:

```bash
veejay --clip-as-sample /path/to/video.avi
```

Start Reloaded and connect to locally running instances:

```bash
reloaded -a
```

Connect to an explicit host and port:

```bash
reloaded --host localhost --port 3490
```

Start Reloaded with four MultiTrack lanes in total. `-X` counts extra tracks after track 0:

```bash
reloaded -a -X 3
```

For current command-line options:

```bash
veejay --help
reloaded --help
man veejay
man reloaded
```

### Media preparation

Media is decoded through FFmpeg/libav. For predictable live sampling and trick-play performance, intraframe video such as MJPEG remains a practical choice. For embedded audio, PCM audio at a project-consistent sample rate is the simplest setup.

Example:

```bash
ffmpeg -i input.mp4 \
    -c:v mjpeg -q:v 2 \
    -c:a pcm_s16le -ar 48000 -ac 2 \
    output.avi
```

## Plugins

### Frei0r

VeeJay searches common Frei0r locations such as:

```text
/usr/local/lib/frei0r-1
/usr/lib/frei0r-1
/usr/lib64/frei0r-1
```

Additional paths can be listed in:

```text
$HOME/.veejay/plugins.cfg
```

### Included plugin packs

- **lvdasciiart** — ASCII-art rendering effects.
- **lvdcrop** — crop and related geometry effects.
- **lvdgmic** — GMIC-based processing; powerful but often more expensive.
- **lvdshared** — producer/consumer effects for shared video resources.

Plugin-specific default parameter files can be stored below:

```text
$HOME/.veejay/frei0r/
$HOME/.veejay/livido/
```

See [`HOWTO.plugins.md`](./veejay-server/doc/HOWTO.plugins.md).

## Development and debugging

Build a component with debug support:

```bash
./autogen.sh
./configure --enable-debug
make -j"$(nproc)"
```

When reporting a crash, include:

- the exact component revisions;
- the server and Reloaded command lines;
- the active playback mode and source;
- the relevant backend and Reloaded logs;
- a debugger backtrace;
- whether JACK/PipeWire, networking, MultiTrack, beat detection, or external sync was active.

Useful documentation:

- [`README.whatis.md`](./veejay-server/doc/README.whatis.md)
- [`README.quickstart.md`](./veejay-server/doc/README.quickstart.md)
- [`HOWTO.compile.md`](./veejay-server/doc/HOWTO.compile.md)
- [`HOWTO.debugging.md`](./veejay-server/doc/HOWTO.debugging.md)
- [`README.audio.md`](./veejay-server/doc/README.audio.md)
- [`README.network.md`](./veejay-server/doc/README.network.md)
- [`README.performance.md`](./veejay-server/doc/README.performance.md)
- [`HOWTO.plugins.md`](./veejay-server/doc/HOWTO.plugins.md)

## Bug reports and contributions

Use the GitHub issue tracker:

<https://github.com/game-stop/veejay/issues>

Patches should keep the VIMS/status contract synchronised across the backend, Reloaded, utilities, Eidolon, and sendVIMS.

## License

VeeJay is Free Software released under the GNU General Public License, version 2 or later. See the `COPYING` files in the source tree.
