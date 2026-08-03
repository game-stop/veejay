# veejay-core

`veejay-core` contains the shared low-level libraries used across the VeeJay
project.

It provides common infrastructure so the server, clients, tools, and related
components can share the same implementations for memory handling, messaging,
networking, timing, frame metadata, audio helpers, and protocol support.

## Main responsibilities

- Common memory allocation and utility helpers
- Logging and message handling
- Networking and socket helpers
- Shared VIMS protocol definitions and support code
- Frame and media-related core data structures
- Audio and timing utilities
- Atomic and thread-related helpers
- Shared constants, definitions, and low-level support routines
- Reusable infrastructure for the rest of the VeeJay codebase

## Role in VeeJay

`veejay-core` is not a standalone application. It is a foundational library
used by higher-level components such as:

- `veejay-server`
- `veejay-client`
- `veejay-utils`
- `veejay-eidolon`
- `veejay-director`

Keeping these common facilities in one library avoids duplicating protocol,
memory, networking, and runtime support code across the project.

## Building

`veejay-core` is normally built as part of the complete VeeJay source tree.

For project-wide build and dependency instructions, refer to the top-level
VeeJay README.
