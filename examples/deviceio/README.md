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
  Video framebuffer:   0x1000–0x2FFF (8192 bytes, 64×32 pixels × 4 bytes)
  Keyboard status:     0x3000
  Audio control:       0x3001
  RNG data:            0x3002
  Audio sample buffer: 0x3010–0x380F (2048 bytes, 512 × int32 samples)
  User memory start:   0x4000
...
```

## Device Memory Map

The VM's address space is divided into three regions:

| Region | Start | End | Size | Description |
|--------|-------|-----|------|-------------|
| Reserved | `0x0000` | `0x0FFF` | 4096 B | Internal use (DIP, temporaries) |
| Video framebuffer | `0x1000` | `0x2FFF` | 8192 B | 64×32 pixels, 4 bytes each (0x00RRGGBB big-endian) |
| Keyboard status | `0x3000` | `0x3000` | 1 reg | Read: direction (0=none 1=up 2=down 3=left 4=right) |
| Audio control | `0x3001` | `0x3001` | 1 reg | Write: send audio command; Read: last value written |
| RNG data | `0x3002` | `0x3005` | 1 reg | Read: next pseudo-random int32 (advances LCG state); Write: seed |
| Audio sample buffer | `0x3010` | `0x380F` | 512 × int32 | Looping PCM; write int32 samples in [-128, 127]; JS plays continuously |
| User memory | `0x4000` | — | remainder | Program and data |

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
// Write pixel color 0x00FF00 (green) to framebuffer pixel 0
prog = append(prog, push(0x00FF00)...)
prog = append(prog, store(vm.VideoFramebufferStart)...)

// Read it back
prog = append(prog, load(vm.VideoFramebufferStart)...)
```

Pixel colors are stored as `0x00RRGGBB` big-endian. Each pixel occupies 4 bytes; pixel `(x, y)` is at `VideoFramebufferStart + (y*64 + x)*4`.

The VM intercepts any `LOAD`/`STORE` whose address falls in `[0x1000, 0x3200)` and routes it through `handleDeviceRead` / `handleDeviceWrite` in `pkg/vm/vm.go`. Accesses outside that range use plain memory directly.

### Keyboard register

The keyboard status register is read-only. Writing to it returns an error:

```
device write error: writing to keyboard status address 4608 is not supported
```

### RNG register

Reading `RNGDataAddr` (`0x3002`) advances an internal LCG state and returns the next pseudo-random `int32`. Writing to it seeds the generator.

### Simulation behaviour

| Device | Read | Write |
|--------|------|-------|
| Video framebuffer | Returns value last written | Stores to `vm.memory` |
| Keyboard status | Returns direction code (0–4); wired to real input in demos | Error |
| Audio control | Returns value last written | Stores to `vm.memory` |
| RNG data | Advances LCG, returns next int32 | Seeds the LCG state |
