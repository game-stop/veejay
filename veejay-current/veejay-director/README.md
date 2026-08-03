# VeeJay Director

**VeeJay Director is the show, routing, venue, and projection-management application for VeeJay.**

Director sits above one or more VeeJay backends. It can launch and supervise local engines, connect to existing local or remote engines, arrange their video routing, configure physical outputs, edit projection meshes, save venue-specific output state, and assist with projector calibration.

Director does not replace VeeJay or Reloaded. VeeJay remains the real-time video engine; Director coordinates the infrastructure around a multi-instance performance or installation.

## Main concepts

Director works with three VeeJay instance roles:

- **Standalone** — a self-contained VeeJay engine for normal playback and output.
- **Program** — a source/program engine intended to feed other VeeJay instances.
- **Output** — a physical-output engine that receives video from a Program instance and owns projector/display mapping.

Program instances can also use **Master** and **Preview** control modes for work-ahead operation.

A Director project is stored as a `.vjd` **show**. Shows contain engine configuration, routing, output setup, projection configuration, venue profiles, snapshots, and related Director state.

## Workbench presets

The startup workbench can create several common configurations:

- **Single VeeJay**
- **Master + Preview**
- **Projector Calibration**
- **Mapped Projector**
- **Dual Projectors + Edge Blend**
- **Master + Preview + Projector**
- **2 × 1 Video Wall**
- **1 × 2 Video Wall**
- **2 × 2 Video Wall**
- **Custom Video Wall**

The dual-projector preset creates one Program canvas feeding left and right Output engines, with overlap and edge blending preconfigured.

Custom video walls support up to 8 × 8 screens, within VeeJay's project-size limits.

## Workspace

The main workspace provides three complementary views of a show:

### Arrange

Arrange engines and physical outputs on the stage. This is useful for representing the actual screen/projector layout and for working with venue geometry.

### Projection

Edit the projection mesh for an Output engine. Projection grids can contain up to 17 × 17 control points and can be manipulated directly in the custom mesh editor.

Projection configuration is kept separately from the logical output slices, so screen placement and projector correction can be managed independently.

### Wiring

Build and inspect video routes between Program and Output engines visually. Director prevents obvious invalid routes such as self-routing and routing cycles.

For local Program → Output routes, Director can use VeeJay's shared-memory path when available. Network-reachable sources can be routed through the configured stream endpoint.

## Output mapping

Each Output can use up to eight mapping slices. A slice defines:

- source crop
- destination rectangle
- enable state
- left/right/top/bottom edge blend
- blend gamma

This allows a Program canvas to be cropped, distributed and blended across projectors or screens while the Output engine remains responsible for the final physical presentation.

Director also manages display selection, fullscreen/windowed state, borderless output and display geometry for managed local instances.

## Venues and snapshots

A **venue profile** captures the physical-output state of a location. Venue data can include:

- output/display assignment
- output geometry
- mapping slices
- projection mesh
- calibration-camera identity
- saved V4L2 camera controls
- dense camera-to-projector calibration maps

This makes it possible to move the same show between locations while restoring the projector-specific configuration for each venue.

**Snapshots** provide a separate mechanism for saving and restoring show/instance state without redefining the venue.

## Camera-assisted projector calibration

Director includes a calibration workflow for projector installations.

A V4L2 capture device can be selected as the calibration camera. Director can query supported camera devices and V4L controls and persist the chosen camera settings with the venue.

The calibration workflow supports:

1. selecting and previewing the camera;
2. locking useful V4L camera controls;
3. displaying projector identification/test patterns;
4. measuring visible projector regions and overlap;
5. assisting alignment;
6. applying the measured overlap to output mapping;
7. running a dense structured-light scan.

### Structured-light mapping

The dense calibration pass displays Gray-code patterns through each Output and observes them with the calibration camera.

The result is a dense **camera-to-projector map**. For valid camera pixels, Director can determine the corresponding projector coordinate. Maps store the camera and projector dimensions together with coverage and confidence information.

Dense maps are stored in the venue profile and checked by the venue preflight system. If projector geometry no longer matches the saved calibration, Director marks the map stale and asks for recalibration.

A fixed camera mounted close to the projector is particularly useful because the saved mapping can later serve as the geometric relationship between the camera's field of view and the projector surface.

## Venue preflight

Before using a saved venue, Director can check important parts of the configuration, including projector-camera maps and real-time frame-budget information.

The preflight view distinguishes ready, warning and failure conditions so configuration problems can be found before output is put live.

## LAN discovery

Director listens for VeeJay backend advertisements on UDP port **3499**.

Discovered engines can appear automatically in the Director engine list. Director distinguishes managed instances, external instances and temporary discovered instances, and detects conflicting advertisements that use the same instance identity.

Director can launch processes only on the local workstation. Remote VeeJay engines are attached as externally managed instances.

## Monitoring and telemetry

Director periodically queries connected VeeJay engines and tracks:

- instance status
- output-graph status
- projection status
- performance telemetry
- previews
- device/V4L information when requested

The Telemetry page displays VeeJay performance data such as producer, renderer and output-graph timing. Director also maintains a live log for process output and management events.

## Process management

For local instances marked **Managed by Director**, Director builds the VeeJay launch command from the configured instance settings and can start and stop the backend.

Director validates Director-owned command-line options so custom arguments cannot silently override settings that Director is responsible for.

The show can optionally launch Reloaded, and Director also contains integration points for Eidolon.

## Show files

Director show files use the `.vjd` extension.

Create or save a show from the Director UI, or open one directly:

```bash
veejay-director
```

```bash
veejay-director /path/to/show.vjd
```

When started without a show file, Director opens its workbench so a built-in setup or saved template can be selected.

Reusable setup templates are stored below the user's VeeJay Director configuration directory.

## Typical projector workflow

A practical single-projector setup is:

1. Create **Mapped Projector** or **Projector Calibration** from the workbench.
2. Assign the physical display to the Output instance.
3. Start/connect the required VeeJay engines.
4. Configure the Output mapping.
5. Open Projection mode and correct the projection mesh.
6. Save the physical setup as a venue.
7. Open Camera Calibration and select the V4L2 camera.
8. Measure/alignment-check the projector.
9. Run structured-light calibration when a dense camera-to-projector map is required.
10. Run venue preflight.
11. Save the `.vjd` show.

For two projectors, **Dual Projectors + Edge Blend** creates the Program → left/right Output topology and initial overlap automatically.

## Requirements

Director expects a compatible VeeJay backend with the Director-facing instance, output-graph, projection, preview, performance and device-control commands available.

The application itself uses GTK3, GLib/GIO and GdkPixbuf. Camera discovery/control is implemented through V4L2 on Linux.

For managed instances, the `veejay` executable must be installed or the executable path must be configured for the instance.

## Relationship to Reloaded

Director and Reloaded solve different problems:

- **Reloaded** is the live VJ performance and editing interface.
- **Director** manages engines, routing, displays, venues, projection and operational infrastructure.

A typical larger setup uses Director to construct and supervise the show topology while Reloaded controls the actual live Program engine.

## License

VeeJay Director is Free Software released under the GNU General Public License, version 2 or later.
