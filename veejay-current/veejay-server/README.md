# VeeJay Server

**VeeJay** is a GNU/Linux real-time video engine, sampler, mixer and programmable visual instrument.

The server is designed for live performance: video can be loaded, cut into samples, looped, scratched, processed through effect chains, mixed with live sources, sequenced, recorded and routed while the engine keeps running.

VeeJay can be used interactively, remotely over the network, as a headless rendering engine, or as one node in a larger multi-instance setup.

## Features

### Real-time video engine

- Soft real-time playback with frame dropping when required to preserve timing.
- Frame-accurate transport and event execution.
- Native planar YUV processing throughout the effect pipeline.
- In-place, low-overhead frame processing designed for live workloads.
- Headless operation for render, routing and server-only installations.
- Dynamic playback rate and frame-rate behavior for real-time trickplay.
- Multi-threaded processing, including OpenMP acceleration in many built-in effects.
- Architecture-targeted builds for performance-sensitive installations.
- Runtime performance telemetry for frame timing, load and output monitoring.
- Hardware accelerated video decoding via Vulkan, VAAPI or NVIDIA Cuda (nvjpeg)

### Live sampling and playback

- Turn source material into instantly playable video samples.
- Independent sample in/out points and loop regions.
- Normal, ping-pong, random and other loop behaviors.
- Forward and reverse playback.
- Variable-speed playback, including slow motion and fast motion.
- Video scratching and direct frame seeking.
- Random frame and random sample playback.
- Live sample creation while the engine is running.
- Samples can use their own edit decision lists instead of destructively modifying source media.
- Large sample banks intended for immediate live recall.

### Edit decision lists and sequencing

- Non-destructive Edit Decision List (EDL) based editing.
- Cut, copy, paste, crop and rearrange source ranges without rewriting original media.
- Project-level and sample-specific EDL workflows.
- Sequence banks for arranging samples into longer performances.
- Backend-owned sequence timeline information with source, duration and slot metadata.
- Frame-synchronous sequence playback tied to the authoritative server transport.
- Persistent VIMS pattern execution for samples, streams, sequence cells and sequence banks.
- Pattern playback follows transport direction, pause/resume, seek, loop and sequence changes.
- VIMS events can be recorded and replayed as part of automated performances.

### Effects and compositing

- Large built-in library of real-time video effects, generators, distortions, temporal effects, keyers, mixers and compositors.
- Multi-slot effect chains.
- Per-effect parameter control while playing.
- Parameter animation and curve-driven modulation.
- Two-source effects and compositing where supported by the selected effect.
- Alpha-aware processing in effects that expose alpha/compositing behavior.
- Temporal and feedback effects with persistent per-instance state.
- Beat-aware parameter metadata for musically useful Auto-FX modulation.
- Effects remain manually controllable and usable without beat detection.

### Audio and JACK

- Low-latency audio output through JACK.
- Audio playback synchronized with video transport.
- Audio follows forward/reverse and variable-speed sample playback where applicable.
- Slow and fast playback can resample audio in the style of variable-speed tape playback.
- Separate handling of source/sample audio and the final JACK-bound output mix.
- Recording can capture the final rendered output audio rather than re-reading source audio.
- Audio timing integrates with dynamic playback speed and frame pacing.

### Media and live inputs

Depending on build configuration and installed libraries, VeeJay can work with file, image, capture and streaming sources, including:

- Video files suitable for frame-accurate playback.
- Raw and YUV-based video sources.
- Still images such as JPEG, PNG and other formats supported by the configured image loader.
- V4L2 cameras and capture devices.
- Multiple live sources within one server session.
- Network-fed and VeeJay-to-VeeJay sources.
- Shared-memory video transports for low-overhead local pipelines.

VeeJay deliberately favors frame-addressable media and live-processing behavior over long-GOP playback semantics.

### Output and routing

- SDL video output in windowed or fullscreen mode.
- Headless/dummy output for server, render and routing use.
- YUV4MPEG output and streaming workflows.
- V4L2 loopback output for feeding VeeJay video into other Linux video applications.
- Shared-memory output for low-latency local inter-process video transport.
- Network video transport using unicast and multicast configurations.
- Preview-frame rendering for remote controllers and monitoring tools.
- Still-image/frame grabbing.
- Multiple output paths can be represented and routed through the server output graph.
- Output instances can target different transports and destinations.
- Projection/output meshes provide server-side geometric warping of rendered video.
- Perspective and quadrilateral mapping for projector alignment and non-rectangular surfaces.
- Split and multi-surface output workflows suitable for video-wall and projection installations.

### VIMS, OSC and remote control

VeeJay is designed to be programmable while it is running.

- **VIMS** — the VeeJay Internal Message System and lowest-level control protocol.
- TCP/IP remote control of transport, samples, streams, effects, parameters, recording and engine state.
- OSC control over UDP.
- Programmable keyboard/event mappings.
- Event recording and playback.
- Server-to-server event forwarding and mirroring.
- Master/follower and multi-instance control workflows.
- Remote sample selection and transport synchronization between VeeJay instances.
- VIMS is suitable for custom controllers, scripts and external systems such as `sendVIMS`.

### Recording and persistence

- Record the live rendered result directly to disk.
- Create new samples from live output while performing.
- Record final rendered video together with final output audio.
- Save and restore sample/deck state.
- Persist sample lists, edit information and related runtime configuration.
- Recovery EDL and sample-list data can be written after fatal failures to improve crash recovery.
- Graceful shutdown handling preserves normal session cleanup paths.

### Plugins and extensibility

- Built-in VeeJay effect API for native effects.
- LiViDO plugin hosting.
- Frei0r plugin support where enabled by the build.
- Optional plugin packs can extend the engine with additional generators and filters.
- External applications can control the complete live engine through VIMS instead of linking directly against the processing core.

## Architecture

VeeJay uses a server-oriented architecture. The engine owns playback state, samples, streams, effect chains, sequencing, audio/video timing and rendering. Remote applications communicate with the running server instead of duplicating this state locally.

VIMS is the central control layer. Higher-level control systems can query server state and issue VIMS events while the backend remains authoritative for transport and frame-synchronous execution.

VeeJay instances can also participate in multi-node setups, exchanging control and video so that several engines can be composed into a larger performance or installation.

## Building

VeeJay Server depends on `veejay-core`.

A typical source build is:

```bash
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
```

To build for a specific CPU target:

```bash
./configure --with-arch-target=generic
./configure --with-arch-target=core2
./configure --with-arch-target=k6
./configure --with-arch-target=native
./configure --with-arch-target=i686
```

For debugging and useful backtraces:

```bash
./configure --enable-debug
```

Debug builds are intended for diagnosis rather than maximum real-time performance.

### Building the bundled LiViDO plugins

```bash
cd livido-plugins
./RUNME.sh /tmp/plugins
```

A plugin directory can then be added to the VeeJay plugin configuration, for example:

```bash
mkdir -p ~/.veejay
echo "/tmp/plugins" >> ~/.veejay/plugins.cfg
```

## Running

Start VeeJay with a media file:

```bash
veejay my-movie-A.avi
```

Start another instance on a different control port:

```bash
veejay -p 4490 my-movie-B.avi
```

VeeJay can also be started with capture, dummy/headless and other input/output modes. Run:

```bash
veejay --help
```

for the options supported by the installed build.

## Documentation

Additional server documentation is available in the `doc/` directory, including installation, compilation, networking, audio, memory and protocol documentation.

Useful starting points include:

- `INSTALL`
- `doc/Installation.md`
- `doc/HOWTO.compile.md`

## License

VeeJay is Free Software released under the GNU General Public License.

“Free” refers to the freedom to run, study, modify and redistribute the software.
