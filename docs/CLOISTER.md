# CLOISTER Graphical Emulator

CLOISTER is the flagship graphical emulator for the NUX Virtual Machine. It provides a rich environment with color graphics, keyboard and mouse support, and a built-in boot sequence.

## Features

### 1. Boot Sequence
Every time CLOISTER starts, it displays the word `CLOISTER` centered on the screen. This sequence represents the initialization of the system's firmware. The user program begins execution only after this sequence completes.

### 2. Display
- **Default Resolution**: 80x80 pixels.
- **Resizability**: The window can be resized via the host OS or set via command line flags.
- **Color**: 32-bit RGBA.
- **Memory Mapping**: The framebuffer starts at `0x4000` (16384). Each pixel occupies 4 bytes.
- **Dynamic Dimensions**: Programs can read the current width and height via MMIO registers.

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

### 5. Configurable Memory and Dimensions
By default, CLOISTER provides **32MB** of RAM and an **80x80** display. These can be customized using command line flags:

```bash
# Set memory to 64MB and screen to 160x100
./bin/cloister -mem 64 -w 160 -h 100 my_game.bin
```

- **`-mem`**: RAM size in MB (max 128MB).
- **`-w`**: Initial screen width.
- **`-h`**: Initial screen height.

## Default Boot State
...
## Hardware Registers (MMIO)
CLOISTER maps hardware state to the `0x3000-0x3FFF` memory region:

| Address | Name | Access | Description |
|---------|------|--------|-------------|
| `0x3024`| SCR_W | R/W | Screen Width |
| `0x3028`| SCR_H | R/W | Screen Height |
| `0x304C`| KBD_K | R | Last key pressed |
| `0x3054`| MSE_X | R | Mouse X coordinate |
| `0x3058`| MSE_Y | R | Mouse Y coordinate |
| `0x305C`| MSE_B | R | Mouse Buttons (1=L, 2=R) |
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
