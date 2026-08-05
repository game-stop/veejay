# Local VeeJay Build and Install

`run-and-install-locally.sh` builds and installs the VeeJay source tree from the repository root.

## Prerequisites

Install all VeeJay build dependencies before running this script. The script does not install compiler packages, development headers, libraries, or other dependencies.

The script also requires:

- Bash
- GNU Make
- Autotools for projects using `autogen.sh`
- `sudo` permission for `make install`

## Expected layout

The script exists beside these directories:

```text
plugin-packs/
sendVIMS/
veejay-client/
veejay-core/
veejay-director/
veejay-eidolon/
veejay-server/
veejay-utils/
run-and-install-locally.sh
```

## Usage

Make the script executable and run it from any directory:

```bash
chmod +x run-and-install-locally.sh
./run-and-install-locally.sh
```

Arguments are forwarded to every `./configure` invocation:

```bash
./run-and-install-locally.sh --enable-debug
```

Multiple configure arguments are supported:

```bash
./run-and-install-locally.sh --enable-debug --prefix=/usr/local
```

## Build behavior

The script:

1. Detects the number of available CPU threads and uses them for parallel builds.
2. Runs `make clean` when a previous build is detected.
3. Runs `autogen.sh`, `./configure`, `make`, and `sudo make install`.
4. Builds `sendVIMS` directly with `make` and `sudo make install`.
5. Stops immediately when a command fails.

The build order is:

```text
veejay-core
veejay-server
veejay-director
veejay-client
veejay-utils
veejay-eidolon
sendVIMS
plugin-packs/lvdasciiart
plugin-packs/lvdcrop
plugin-packs/lvdgmic
plugin-packs/lvdshared
```

To override automatic CPU detection:

```bash
JOBS=8 ./run-and-install-locally.sh --enable-debug
```

To build with NDI
```bash
JOBS=8 ./run-and-install-locally.sh --with-ndi=/opt/ndi6
```

To run with NDI, point NDI_RUNTIME_DIR_V6 to where you installed NDI

```bash
NDI_RUNTIME_DIR_V6=/opt/ndi6/bin/x86_64-linux-gnu veejay --ndi-list
```

