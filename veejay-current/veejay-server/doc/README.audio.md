```
       ,---,
      '  .' \                         ,---,  ,--,
     /  ;    '.             ,--,    ,---.'|,--.'|    ,---.
    :  :       \          ,'_ /|    |   | :|  |,    '   ,'\
    :  |   /\   \    .--. |  | :    |   | |`--'_   /   /   |
    |  :  ' ;.   : ,'_ /| :  . |  ,--.__| |,' ,'| .   ; ,. :
    |  |  ;/  \   \|  ' | |  . . /   ,'   |'  | | '   | |: :
    '  :  | \  \ ,'|  | ' |  | |.   '  /  ||  | : '   | .; :
    |  |  '  '--'  :  | : ;  ; |'   ; |:  |'  : |_|   :    |
    |  :  :        '  :  `--'   \   | '/  '|  | '.'\   \  /
    |  | ,'        :  ,      .-./   :    :|;  :    ;`----'
    `--''           `--`----'    \   \  /  |  ,   /
                                  `----'    ---`-'
```

# Audio support

Veejay is primarily a video sampler and performance engine. Its audio support is intentionally practical: it keeps clip audio in transport sync, can route audio through a JACK-compatible audio graph, and can use audio as a control source for beat-driven FX and audio-sync modes.

Veejay does not try to be a DAW. There are no complex sound-effect chains. The audio system exists to keep video performance, clip playback, audio monitoring, recording taps, and beat/audio-control routing coherent.

## Supported audio backend

Veejay uses the JACK API. On a modern Linux desktop this can mean any of these setups:

- **PipeWire with JACK compatibility**, usually via `pw-jack`
- **jackd** or **jackdbus**
- a session manager / patchbay such as **qpwgraph**, **Helvum**, **Carla**, or **qjackctl**

So the old rule “Veejay only supports jackd1” is no longer the useful way to think about it. What matters is that a JACK-compatible server is available and that Veejay can connect to it.

Check that Veejay was built with JACK support:

```sh
veejay -B | grep -i jack
```

If this prints nothing, rebuild `veejay-server` with JACK support enabled.

## Preparing media

For the most predictable embedded-audio playback, use AVI/MJPEG video with signed 16-bit PCM audio. Stereo 44.1 kHz or 48 kHz is recommended.

Example conversion:

```sh
ffmpeg -i myvideo.mp4 \
  -q:v 1 -vcodec mjpeg \
  -acodec pcm_s16le -ar 48000 -ac 2 \
  -s 1024x576 \
  myvideo.avi
```

This creates:

- MJPEG video
- signed 16-bit little-endian PCM audio
- stereo audio
- 48 kHz sample rate
- 1024x576 output video

For a project, keep all source files at the same audio rate where possible. Mixed source rates can work through resampling paths, but a single project rate is easier to reason about and easier to keep tight.

## Starting audio

### PipeWire / pw-jack

On PipeWire systems, the usual launch pattern is:

```sh
pw-jack veejay -m80 /path/to/myvideo.avi
```

If you start Veejay from Reloaded or scripts, make sure the process inherits the PipeWire JACK environment. When in doubt, start both server and client from a shell that uses `pw-jack`.

Useful graph tools:

```sh
qpwgraph
helvum
qjackctl
```

Use these to verify that Veejay's JACK ports are connected to the desired output device.

### jackd / qjackctl

You can also start a JACK server manually:

```sh
jackd -dalsa -P -r48000
```

or configure it through `qjackctl`.

Choose a sample rate that matches your media or project expectation:

```text
44.1 kHz media  -> JACK rate 44100
48 kHz media    -> JACK rate 48000
```

## Starting Veejay

```sh
veejay -m80 /path/to/myvideo.avi
```

The `-m` option allows Veejay to cache video frames when sampling. This reduces disk pressure and helps audio remain smooth when sampling, scrubbing, changing speed, or using pitch/transport-heavy workflows.

See also:

```text
README.memory.md
```

## Audio-related options

Common options:

```text
-a, --audio [0|1]             Disable or enable audio playback
-c, --synchronization [0|1]   Disable or enable sync correction
-r, --audiorate <rate>        Set the expected audio rate
    --pace-correction <ms>    Audio pace correction offset in milliseconds
```

Run this for the current option list:

```sh
veejay --help
man veejay
```

## Embedded clip audio

When playing a video file with an embedded PCM audio track, Veejay decodes the clip audio and writes it to the JACK playback route. The video transport remains the master conceptually, but the runtime uses audio clocking and queue state to keep playback smooth.

If the loaded file has no usable audio track, Veejay cannot produce embedded clip audio for that source. In that case JACK may still be used for external audio/control routes, depending on the selected mode and runtime configuration.

## Audio routes and taps

Veejay has several separate audio concerns. They should not be confused:

### 1. Playback audio

This is the audio you hear from the currently playing clip or selected audio route. It is written to JACK/PipeWire and appears in the system audio graph.

### 2. JACK / output volume

This controls the outgoing JACK playback level. It is not the same thing as sample audio mixing.

### 3. Sample audio / recording policy

Recording can use different audio sources depending on policy, such as original clip audio, external/beat JACK source, or silence.

### 4. Audio mixing

The audio mixer decides how original clip audio and external audio routes are combined or followed. This is separate from JACK volume.

### 5. Beat detector / audio control tap

The beat detector can analyze an audio source and publish control signals for FX automation. When the beat detector is set to the original source, it receives a copy of the outgoing original audio. It does not need to be in the audible signal path to control effects.

## Beat detector latency

The latency value shown by the UI is best understood as **beat compensation latency** or **heard audio latency**, not as pure detector processing time.

For example, the backend may report a value around 300 ms when using a Bluetooth output device. That does not mean the beat analyzer itself needs 300 ms to detect audio. It means the backend is compensating against the full path to what the performer hears:

```text
Veejay playback queue
+ JACK/PipeWire buffering
+ device latency
+ Bluetooth/output latency, if present
```

This is useful for beat-driven FX. If audio reaches your ears late, visual beat response must be offset against that heard timing.

For lower practical latency, prefer wired audio hardware and smaller JACK/PipeWire periods.

## Ringbuffer sizing

Veejay's JACK bridge uses playback and capture ringbuffers. These buffers absorb scheduling jitter and video processing spikes. Larger buffers are safer. Smaller buffers reduce latency but increase the risk of underruns, clicks, or garbled audio.

The playback/capture sizing can be built with these defaults:

```c
#ifndef VJ_JACK_PLAYBACK_RING_VIDEO_FRAMES
#define VJ_JACK_PLAYBACK_RING_VIDEO_FRAMES 2
#endif
#ifndef VJ_JACK_PLAYBACK_RING_MIN_PERIODS
#define VJ_JACK_PLAYBACK_RING_MIN_PERIODS 4
#endif
#ifndef VJ_JACK_CAPTURE_RING_VIDEO_FRAMES
#define VJ_JACK_CAPTURE_RING_VIDEO_FRAMES 2
#endif
#ifndef VJ_JACK_CAPTURE_RING_MIN_PERIODS
#define VJ_JACK_CAPTURE_RING_MIN_PERIODS 4
#endif
```

The startup log reports both the requested size and the actual capacity, because JACK ringbuffers may round the allocation:

```text
[AUDIO]: Jack playback ringbuffer requested 4864 frames, capacity 8192 frames (... ms)
```

That actual capacity is the important number.

## Latency notes

Latency comes from several places:

```text
Veejay audio queue
+ JACK/PipeWire period size
+ JACK/PipeWire graph buffering
+ resampling delay, when rates differ
+ audio device latency
+ Bluetooth latency, if used
```

To reduce latency:

- use a wired audio output instead of Bluetooth
- use a smaller JACK/PipeWire period size
- keep media and JACK/PipeWire at the same sample rate
- avoid very heavy FX chains when audio stability matters
- cache samples with `-m` when disk I/O is a bottleneck
- use a realtime or low-latency kernel setup where practical

Do not blindly shrink Veejay's internal ringbuffer. Too small a playback ring can garble audio when the video pipeline has a scheduling spike.

## Troubleshooting

### No audio

Check:

```sh
veejay -B | grep -i jack
```

Then inspect the JACK/PipeWire graph:

```sh
qpwgraph
helvum
qjackctl
```

Make sure Veejay output ports are connected to the intended playback device.

### Audio sounds delayed

Check the output device first. Bluetooth devices can add significant latency. The UI beat latency may correctly reflect this as heard-output compensation.

Prefer a wired interface for performance.

### Audio is garbled or crackles

Possible causes:

- JACK/PipeWire period too small
- Veejay playback ring too small
- CPU overload from heavy FX chains
- disk I/O pressure while sampling
- sample-rate mismatch causing extra resampling load
- Bluetooth device switching profiles or buffering heavily

Try:

```sh
veejay -m80 /path/to/myvideo.avi
```

or increase the JACK/PipeWire period/buffer size.

### Warning: rendering audio/video frame takes too long

Example warning:

```text
Rendering audio/video frame takes too long. Can't keep pace with audio.
```

Possible fixes:

1. Use lighter FX or lower the video resolution.

2. Use faster storage or ensure the sample is cached.

3. Use a dedicated Veejay server and connect with Reloaded over the network.

4. Increase JACK/PipeWire buffer size if low latency is less important than stability.

## Practical recommendation

For live use:

```text
PipeWire/JACK-compatible graph
wired audio output
48 kHz project/media rate
moderate JACK/PipeWire period
Veejay sample cache enabled
Bluetooth avoided for performance monitoring
```

For casual previewing, Bluetooth can be fine. For beat-synced performance, use wired audio if you want low perceived latency.
