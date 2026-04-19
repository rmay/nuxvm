# Device I/O Examples

Demonstrates memory-mapped device I/O on the NUXVM stack VM.

## Running

```bash
go run ./examples/deviceio/
```

From the repo root. Expected output:

```
╔══════════════════════════════════════════════════════════╗
║           Device I/O Examples — NUXVM                   ║
╚══════════════════════════════════════════════════════════╝

╔══ EXAMPLE 6: Device Memory Map ══╗
  Reserved memory:     0x0000–0x0FFF (4096 bytes)
  Video framebuffer:   0x1000–0x11FF (512 bytes)
  Keyboard status:     0x1200
  Audio control:       0x1201
  User memory start:   0x1400
...
```

## Device Memory Map

The VM's address space is divided into three regions:

| Region | Start | End | Size | Description |
|--------|-------|-----|------|-------------|
| Reserved | `0x0000` | `0x0FFF` | 4096 B | Internal use (DIP, temporaries) |
| Video framebuffer | `0x1000` | `0x11FF` | 512 B | Pixel/display data |
| Keyboard status | `0x1200` | `0x1200` | 1 reg | Read: 1 if key held, 0 otherwise |
| Audio control | `0x1201` | `0x1201` | 1 reg | Write: send audio command; Read: last value written |
| User memory | `0x1400` | — | remainder | Program and data |

Reads and writes to the device region go through device handlers in `pkg/vm/vm.go`. Accesses outside this region use plain memory.

## Examples

| # | Name | What it shows |
|---|------|---------------|
| 1 | Video Framebuffer Write/Read | Write a pixel value, read it back via LOAD |
| 2 | Fill Framebuffer Row | Write 8 pixels and verify with LOAD |
| 3 | Keyboard Status Read | Read the keyboard register (simulated as always-pressed) |
| 4 | Audio Control Write | Write a frequency (440 Hz) and confirm with a read-back |
| 5 | Framebuffer Scan | Write a sentinel at pixel 5 and scan the row |
| 6 | Device Memory Map | Print the base address of each device region |
| 7 | Normal Memory Unaffected | Confirm stores to user memory work normally |
| 8 | Keyboard Poll Loop | Poll the keyboard register 3 times using a counter loop |

## How Device I/O Works

Programs interact with devices using the standard `LOAD` and `STORE` instructions with device-region addresses:

```go
// Write pixel value 0xFF0000 to framebuffer[0]
prog = append(prog, push(0xFF0000)...)
prog = append(prog, store(vm.VideoFramebufferStart)...)

// Read it back
prog = append(prog, load(vm.VideoFramebufferStart)...)
```

The VM intercepts any `LOAD`/`STORE` whose address falls in `[0x1000, 0x1400)` and routes it through `handleDeviceRead` / `handleDeviceWrite` in `pkg/vm/vm.go`. Accesses outside that range use plain memory directly.

### Keyboard register

The keyboard status register is read-only. Writing to it returns an error:

```
device write error: writing to keyboard status address 4608 is not supported
```

### Simulation behaviour

| Device | Read | Write |
|--------|------|-------|
| Video framebuffer | Returns value last written | Stores to `vm.memory` |
| Keyboard status | Returns 1 (key always simulated as pressed) | Error |
| Audio control | Returns value last written | Stores to `vm.memory` |
