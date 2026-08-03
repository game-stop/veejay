# VeeJay Reloaded

**Reloaded** is the GTK3 live-performance and editing client for [VeeJay](../veejay-server/README.md).
It provides a graphical control surface for performing, sampling, sequencing, effect manipulation, MIDI control, automation, and detailed editing while the VeeJay server performs the real-time media processing.

Reloaded is intentionally a client: the VeeJay server may keep running while Reloaded connects, disconnects, or reconnects.

## Features

### Live performance

- Fast GTK3 interface designed for live VJ performance.
- Connect to VeeJay over the VIMS/TCP control protocol.
- Live playback control with frame-accurate seeking.
- Playback speed, reverse, slow-motion, scratching, pause, start and stop controls.
- Loop control, including normal, ping-pong, no-loop and random modes where supported by the active source.
- In/out points, markers and sample-position editing.
- Live sample recording and sampling workflows.
- Direct switching between samples, streams and patterns.
- Keyboard-driven operation for frequently used performance actions.
- Server-authoritative status handling keeps important UI state synchronized with the running VeeJay instance.

### Sample and media banks

- Visual sample-bank interface for rapid live selection.
- Sample thumbnails and preview caching.
- Sample, stream and pattern-oriented workflows.
- Sample playback, loop, speed and audio controls directly from the client.
- Drag-and-drop oriented editing in supported views.
- Sequence and pattern state surfaced alongside media controls where relevant.

### Effects and automation

- Build and manipulate VeeJay FX chains from the GUI.
- Enable, disable, reorder and edit effect-chain entries.
- Full parameter control using sliders, spin controls and other context-appropriate widgets.
- Parameter range and value feedback from the connected server.
- Curve-based parameter automation and predefined curve shapes.
- Manual FX control remains available independently of beat-driven automation.
- Beat-hint aware Auto-FX control can drive musically useful effect parameters without replacing normal manual operation.
- FX presets and reusable performance setups can be managed from the live interface.

### Sequence editor

Reloaded includes a dedicated performance-oriented sequence editor rather than treating sequencing as a simple playlist.

- Four Sequence banks.
- Up to 120 slots per bank.
- Grid-based sequence performance and editing.
- Queueing and **Wait End** style transitions for controlled hand-off between items.
- Range selection and multi-item editing.
- Keyboard shortcuts and context-menu operations.
- Recording-oriented rows and controls for live sequence construction.
- Pattern indicators make mixed sequence/pattern arrangements easier to read during a performance.

### VIMS Pattern editor

The Pattern editor turns VIMS commands into reusable, editable performance sequences.

- Eight VIMS tracks (`V1` through `V8`).
- Step-based event editing.
- Per-pattern looping and playback control.
- Hidden/internal event handling without cluttering normal editing.
- **Learn** workflows for capturing live actions into patterns.
- **Follow** mode for keeping the editor aligned with playback.
- Undo/history support for pattern editing.
- Mouse and keyboard editing workflows.
- Drag-and-drop of captured VIMS events into pattern cells.
- Patterns can be integrated with the Sequence banks for larger arrangements.

### VIMS history and command workflows

Reloaded exposes the VIMS control layer as a practical live-editing tool rather than requiring users to hand-write protocol messages.

- Captures VIMS commands produced by GUI and MIDI interaction.
- Preserves the effective command sent to VeeJay.
- Repeated commands can be collapsed to keep history readable.
- History navigation, clearing and copying.
- Drag captured commands directly into Pattern-editor cells.
- Double-click/keyboard insertion workflows for rapid pattern construction.
- Playback-generated events are kept from recursively recording themselves back into the history workflow.

### MIDI control

Reloaded supports ALSA MIDI controllers with learnable mappings.

- MIDI learn mode from the graphical interface.
- Map hardware controls to transport, sample, FX and parameter actions.
- FX mappings retain the target sample/chain-entry/parameter context while the incoming controller value remains dynamic.
- Save and reuse MIDI layouts.
- Start Reloaded with an existing MIDI configuration using `-m` / `--midi`.
- MIDI-generated actions participate in the same VIMS command and history workflows as GUI actions.

### Beat and Auto-FX control

- Dedicated beat/performance controls.
- Live BPM and beat-state feedback.
- Beat hit and beat-age information.
- Audio sample-rate/status feedback where available.
- Beat-driven Auto-FX modulation using effect-provided beat hints.
- Break-beat and tempo-oriented performance workflows.
- External audio/tempo synchronization support through the VeeJay audio stack.

Beat automation is additive: normal effect movement and manual parameter control remain usable when beat detection is disabled.

### Audio controls

Reloaded keeps the major audio control domains separate in the UI:

- JACK/output volume control.
- Per-sample audio volume control.
- Audio mixing controls.

This avoids conflating output gain, source level and mix behavior during live operation.

### Multitrack editing

- GTK3 multitrack editing interface for arranging recorded and sampled material.
- Timeline and time-range selection tools.
- Context-menu and keyboard editing operations.
- Integration with VeeJay's playback and sample workflows.
- Designed for detailed editing without leaving the live-control environment.

### Live feedback and monitoring

- Continuous VeeJay status monitoring.
- UI state follows confirmed server state for operations where acknowledgement matters.
- Preview-cache infrastructure keeps media browsing responsive.
- Live meters and status indicators for performance-critical controls.
- Connection status and server feedback remain visible while performing.

## Architecture

Reloaded is a network client for VeeJay. The server owns the real-time video/audio processing, samples, streams, effects and playback state; Reloaded provides the interactive control and editing surface.

Communication is primarily performed through **VIMS**, VeeJay's command and status protocol. This separation makes it possible to run the GUI and engine as distinct processes and to reconnect the client without treating the GUI as the media engine itself.

## Running

Show the available command-line options:

```bash
reloaded --help
```

A compact auto-connect startup using the default theme can be launched with:

```bash
reloaded -a -t default -S
```

Reloaded can connect and disconnect while the VeeJay server remains running.

## MIDI setup

Reloaded exposes an ALSA MIDI input. A controller can be connected with standard ALSA tools such as `aconnect`.

Start Reloaded:

```bash
reloaded -a
```

List MIDI input devices:

```bash
aconnect -i
```

List MIDI destinations:

```bash
aconnect -o
```

Connect the controller to the Reloaded/VeeJay MIDI port. For example:

```bash
aconnect 129 128
```

Inspect the resulting connections:

```bash
aconnect -l
```

Then enable **MIDI Learn** in Reloaded and operate the GUI control you want to bind together with the desired hardware control. Saved MIDI layouts can later be loaded with:

```bash
reloaded --midi <layout-file>
```

ALSA client and port numbers are assigned dynamically; use `aconnect -i`, `aconnect -o` and `aconnect -l` to determine the values for the current session.

## Installation and building

See the project installation/build documentation and the files shipped with the source tree, including:

- `INSTALL`
- [`../veejay-server/doc/Installation.md`](../veejay-server/doc/Installation.md)
- [`../veejay-server/doc/HOWTO.compile.md`](../veejay-server/doc/HOWTO.compile.md)

Reloaded is built as part of the VeeJay source tree and requires the corresponding VeeJay libraries and development dependencies.

## Reporting issues

Bug reports and development issues are tracked in the VeeJay repository:

- <https://github.com/game-stop/veejay/issues>
- <https://github.com/game-stop/veejay/labels/reloaded>

When reporting a Reloaded issue, include the VeeJay/Reloaded revision, relevant console output, the steps required to reproduce the problem, and whether the issue also occurs after reconnecting to a fresh VeeJay server instance.

## About

Reloaded is the graphical live-control client for VeeJay. It is designed to make the engine's deep real-time performance and editing capabilities accessible without coupling the lifetime of the media engine to the graphical interface.
