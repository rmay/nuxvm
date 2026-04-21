# Device I/O Examples

Demonstrates memory-mapped device I/O on the NUXVM stack VM.

## Interactive demo

```bash
go run ./examples/deviceio/demo/
```

A pixel bounces around a 64 × 32 framebuffer rendered as block characters in the
terminal. Press **any key** to exit.

The demo shows all three device I/O mechanisms working together:

- **Video framebuffer** — the VM erases and redraws a pixel each frame using
  `OpStoreI` (indirect store; address computed at runtime from x/y state in
  reserved memory)
- **Keyboard register** — wired to real stdin via a goroutine; the VM polls it
  every frame with `LOAD KeyboardStatusAddr` and halts when a key is pressed
- **OpYield** — the VM calls `YIELD` at the end of each frame, which triggers
  the host's render + 15 fps sleep before resuming execution

## Static examples

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
  Device ports:        0x3000–0x3FFF (16-byte blocks)
  Video framebuffer:   0x4000–0x5FFF (8192 bytes, 64×32 pixels × 4 bytes)
  User memory start:   0x6000
...
```

## Device Memory Map

The VM's address space is divided into several regions:

| Region | Start | End | Size | Description |
|--------|-------|-----|------|-------------|
| Reserved | `0x0000` | `0x0FFF` | 4KB | Internal use (DIP, temporaries) |
| Device Ports | `0x3000` | `0x3FFF` | 4KB | Standardized 16-byte I/O ports |
| Video framebuffer | `0x4000` | `0x5FFF` | 8KB | 64×32 pixels, 4 bytes each (0x00RRGGBB big-endian) |
| User memory | `0x6000` | — | remainder | Program and data |

### Standard Ports

Starting at `0x3000`:

| Port | Device | Description |
|------|--------|-------------|
| `0x3000` | System | VM control and metadata |
| `0x3010` | Console | Standard I/O characters |
| `0x3020` | Screen | Graphics control |
| `0x3030` | Audio | Sound synthesis trigger |
| `0x3040` | Keyboard| Input status and keys |
| `0x3050` | Mouse | Cursor position and buttons |
| `0x3060` | RNG | Random number seed/read |

Reads and writes to the device or video regions go through device handlers in `pkg/vm/vm.go`. Accesses outside these regions use plain memory directly.

## Examples

| # | Name | What it shows |
|---|------|---------------|
| 1 | Video Framebuffer Write/Read | Write a pixel value, read it back via LOAD |
| 2 | Fill Framebuffer Row | Write 8 pixels and verify with LOAD |
| 3 | Keyboard Status Read | Read the keyboard register (simulated as always-pressed) |
| 4 | Audio Control Write | Write a sound ID and confirm with a read-back |
| 5 | Framebuffer Scan | Write a sentinel at pixel 5 and scan the row |
| 6 | Device Memory Map | Print the base address of each device region |
| 7 | Normal Memory Unaffected | Confirm stores to user memory work normally |
| 8 | Keyboard Poll Loop | Poll the keyboard register 3 times using a counter loop |

## How Device I/O Works

Programs interact with devices using the standard `LOAD` and `STORE` instructions with device-region addresses:

```go
// Write pixel color 0x00FF00 (green) to framebuffer pixel 0
prog = append(prog, push(0x00FF00)...)
prog = append(prog, store(vm.VideoFramebufferStart)...)

// Read it back
prog = append(prog, load(vm.VideoFramebufferStart)...)
```

Pixel colors are stored as `0x00RRGGBB` big-endian. Each pixel occupies 4 bytes; pixel `(x, y)` is at `VideoFramebufferStart + (y*64 + x)*4`.

### Keyboard port

The keyboard status register (`0x3040`) is read-only. Writing to it returns an error.

### RNG port

Reading the RNG data register (`0x3060`) advances an internal **Xorshift32** state and returns the next pseudo-random `int32`. Writing to it seeds the generator.

### Simulation behaviour

| Device | Read | Write |
|--------|------|-------|
| Video framebuffer | Returns value last written | Stores to `vm.memory` |
| Keyboard status | Returns direction code (0–4); wired to real input in demos | Error |
| Audio control | Returns value last written | Triggers SoundHandler |
| RNG data | Advances state, returns next int32 | Seeds the state |
