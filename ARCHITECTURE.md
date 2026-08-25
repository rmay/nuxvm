# Architectural Analysis: Migrating Cloister towards Plan 9

This document outlines the architectural changes required to shift Cloister (the tiny OS running on NUXVM) away from the Uxn/Varvara model and towards a design heavily inspired by Plan 9. 

## Current State Analysis

NuxVM currently implements **two competing hardware abstraction layers (HALs)**:

1. **Varvara-style Memory-Mapped I/O (MMIO):**
   - Devices like Screen, Audio, Controller, and Mouse are accessed via fixed memory ports (e.g., `screenPort = 0x0020`, `mousePort = 0x0050`).
   - Programs poll these ports or rely on hardware vectors (`ScreenVectorIdx`, `MouseVectorIdx`) to jump to interrupt routines.

2. **Plan 9-style Virtual File System (VFS):**
   - Implemented via a System Call Interface (SCI) on port `0x00D0`.
   - Supports `open`, `read`, `write`, `close`, and `bind`.
   - Exposes synthetic files like `/sys/draw`, `/sys/audio`, `/sys/kbd`, `/sys/mouse`, and `/sys/vm/new`.
   - Supports per-VM namespaces via the `bind` command.

The presence of both paradigms makes the architecture split. To fully embrace Plan 9, the OS must adopt the "Everything is a File" philosophy.

## Proposed Architecture

### 1. Deprecate and Remove Varvara MMIO Devices
The fixed-port memory mapped devices (Screen, Audio, Controller, Mouse, File, GPU) should be removed. All I/O should exclusively flow through the VFS. 
- `ScreenPort`, `MousePort`, etc., and their associated `Vector` interrupt handlers should be eliminated.
- Programs should open `/dev/draw` (or `/sys/draw`), `/dev/mouse`, and `/dev/kbd`.

### 2. Event-Driven I/O via Blocking Reads
In Varvara, you set a vector and the VM jumps to it when the mouse moves. In Plan 9, a program reads from `/dev/mouse`.
- **Recommendation:** Implement blocking reads. When a program calls `read()` on `/dev/mouse` and there are no events, the VM should automatically `YIELD` its execution slice until data is available. 
- This eliminates the need for busy-polling and interrupt vectors, leading to a clean, sequential programming model.

### 3. Single-app host
Cloister is the fantasy machine (uxnemu / Varvara), not rio. One ROM owns the 960×720 screen. `/sys/vm/new` still exists on the VFS for experiments; the graphical host does not spawn child windows.

### 4. 9P Protocol / Network Transparency
While the current VFS is internal to the C host, Plan 9 is built on the 9P protocol, which allows VFS trees to be exported over the network.
- **Recommendation:** Standardize the byte formats for reading/writing to device files so that they can be easily marshaled into 9P messages later.
- Allow host directories (e.g., the Mac file system) to be mounted into the VM via 9P, rather than the current host-file implementation which opens paths with POSIX `open`/`read`/`write` directly in the VFS.

### 5. Namespaces and `bind`
The current `BindFile` implementation is a great start. It needs to be extended to support union mounts (Plan 9's `bind -a` and `bind -b`), allowing a program to stack directories (e.g., mounting a new app's `bin/` directory on top of the system `/bin`).

## User Review Required

> [!IMPORTANT]
> To proceed, we need to decide on the immediate refactoring scope. Should we:
> 1. Keep the MMIO ports for backwards compatibility while we flesh out the VFS, OR
> 2. Hard-break the MMIO ports now and fully transition all existing `.lux` programs to use VFS file descriptors for drawing and input?

Answer: Hardbreak

## Open Questions

> [!WARNING]  
> 1. **Input multiplexing**: In Plan 9, reading from `/dev/mouse` produces formatted text strings (e.g., `m 100 200 1 0` for mouse x,y,buttons,msec). Do you want to use textual representations for events (Plan 9 style) or stick to binary structs for performance?
> 2. **Concurrency**: Should the VM support a `fork` opcode, or should concurrency be handled entirely by spawning new child VMs via `/sys/vm/new`?

Answer: Binary. Spawning new children.
