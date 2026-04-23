# CLOISTER Graphical Emulator

CLOISTER is the flagship graphical emulator for the NUX Virtual Machine. It provides a rich environment with color graphics, keyboard and mouse support, and a built-in boot sequence.

## Features

### 1. Boot Sequence
Every time CLOISTER starts, it displays the word `CLOISTER` centered on the screen. This sequence represents the initialization of the system's firmware. The user program begins execution only after this sequence completes.

### 2. Display
- **Resolution**: 64x32 pixels.
- **Color**: 32-bit RGBA.
- **Memory Mapping**: The framebuffer starts at `0x4000` (16384). Each pixel occupies 4 bytes.

### 3. Mouse Support
- **Cursor**: A hardware-rendered crosshair cursor is always visible at the system's mouse position.
- **Interaction**: Mouse coordinates and button states are accessible via the `MOUSE` module in `lib/system.lux`.
- **Registers**:
    - `0x3054`: Mouse X coordinate (read-only)
    - `0x3058`: Mouse Y coordinate (read-only)
    - `0x305C`: Mouse Buttons bitmask (read-only, 1=Left, 2=Right)

### 4. Keyboard Support
- **Interaction**: Keyboard events trigger the Controller Vector.
- **Registers**:
    - `0x304C`: Last key pressed (read-only)
    - `0x3040`: Vector address for key events (write-only)

### 5. Configurable Memory
By default, CLOISTER provides **32MB** of RAM. This can be customized using the `-mem` flag:

```bash
# Set memory to 64MB
./bin/cloister -mem 64 my_game.bin
```

Memory is capped at **128MB** for system safety.

## Default Boot State
If CLOISTER is run without any program arguments, it automatically loads `lib/boot.bin`. This default program:
1. Clears the screen to a Cyan background.
2. Sets up a keyboard listener.
3. Prints a "CLOISTER Booted." message to the console.
4. Enters an idle loop waiting for input.

## Interaction via LUX
To interact with CLOISTER hardware, it is recommended to use the standard library:

```forth
INCLUDE "lib/system.lux"
IMPORT SCREEN
IMPORT MOUSE

@draw-at-mouse
    MOUSE::x@ MOUSE::y@ 0xFFFFFF SCREEN::pixel!
;
```
