# System Call Interface (SCI) Design

## Overview

The System Call Interface (SCI) is a standardized protocol for Lux programs to interact with OS services. It's implemented as a port-based memory-mapped interface that routes requests to concurrent OS service goroutines.

## Architecture

### Communication Flow

```
Lux Code
  ↓
SCI Word (e.g., CREATE-WIN)
  ↓
Compile to LOAD + port write instructions
  ↓
CPU executes writes to SCI port (0x30C0+)
  ↓
System.Write() intercepts the write
  ↓
handleSCICommand() routes to appropriate handler
  ↓
Handler queries/modifies Services state or sends IPC message
  ↓
SCI Result register (0x30C0) contains result
  ↓
Lux Code reads result
```

## Port Structure

The SCI device occupies a 16-byte block starting at 0x30C0:

| Offset | Address | Name      | Access | Purpose |
|--------|---------|-----------|--------|---------|
| +0     | 0x30C0  | SCI-PORT  | R      | Result/Vector - returns result of last SCI call |
| +4     | 0x30C4  | SCI-CMD   | W      | Command code (1-16) |
| +8     | 0x30C8  | SCI-ARG1  | W      | First argument |
| +12    | 0x30CC  | SCI-ARG2  | W      | Second argument (writing this triggers command) |

## Command Codes

### Window Management (1-8)

#### 1. SCI-CREATE-WIN
Create a new window.
```
Arguments:
  arg1: pointer to null-terminated window name string
  arg2: size (width << 16 | height)
Returns:
  window ID (≥ 0) or -1 on error
```

#### 2. SCI-CLOSE-WIN
Close a window.
```
Arguments:
  arg1: window ID
Returns:
  0 on success, -1 on error
```

#### 3. SCI-MOVE-WIN
Move a window to a new position.
```
Arguments:
  arg1: window ID
  arg2: position (x << 16 | y)
Returns:
  0 on success, -1 on error
```

#### 4. SCI-DRAW-RECT
Draw a filled rectangle (reserved for future use).
```
Arguments:
  arg1: window ID
  arg2: rectangle data
Returns:
  status
```

#### 5. SCI-DRAW-TEXT
Draw text (reserved for future use).
```
Arguments:
  arg1: window ID
  arg2: text data pointer
Returns:
  status
```

#### 6. SCI-SET-PIXEL
Set a single pixel in a window.
```
Arguments:
  arg1: window ID
  arg2: pixel position (x | y << 16)
  [stored in SCI-ARG2 before write]
Returns:
  0 on success, -1 on error
Color is passed via the vector register write trigger.
```

#### 7. SCI-GET-WIN-SIZE
Get the dimensions of a window.
```
Arguments:
  arg1: window ID
Returns:
  size (width << 16 | height)
```

#### 8. SCI-FOCUS-WIN
Set the active window (bring to front).
```
Arguments:
  arg1: window ID
Returns:
  0 on success, -1 on error
```

### Input (9)

#### 9. SCI-POLL-EVENT
Poll for the next input event.
```
Arguments:
  (none)
Returns:
  event packed as (type << 24 | data), or 0 if no event
Event types:
  0 = KeyDown
  1 = KeyUp
  2 = MouseMove
  3 = MouseDown
  4 = MouseUp
```

### File I/O (10-13)

#### 10. SCI-OPEN-FILE
Open a file.
```
Arguments:
  arg1: pointer to null-terminated file path
Returns:
  file handle (≥ 0) or -1 on error
```

#### 11. SCI-READ-FILE
Read from an open file (reserved for full implementation).
```
Arguments:
  arg1: file handle
  arg2: number of bytes to read
Returns:
  bytes read or -1 on error
```

#### 12. SCI-WRITE-FILE
Write to an open file (reserved for full implementation).
```
Arguments:
  arg1: file handle
  arg2: number of bytes to write
Returns:
  bytes written or -1 on error
```

#### 13. SCI-CLOSE-FILE
Close an open file.
```
Arguments:
  arg1: file handle
Returns:
  0 on success, -1 on error
```

### Sound (14)

#### 14. SCI-PLAY-SOUND
Play a sound effect.
```
Arguments:
  arg1: sound ID (treated as frequency in Hz)
Returns:
  0 on success
```

### Process Control (15-16)

#### 15. SCI-YIELD
Voluntarily yield CPU time.
```
Arguments:
  (none)
Returns:
  0
This signals the VM to stop executing until the next host frame.
```

#### 16. SCI-GET-PID
Get the current process ID.
```
Arguments:
  (none)
Returns:
  process ID (currently always 1)
```

## Usage in Lux

### Basic Example

```lux
( Create a window )
"MyWindow" CREATE-WINDOW -> win-id

( Set a pixel to red at (100, 200) )
win-id 100 200 RED SET-PIXEL

( Poll for an event )
POLL-EVENT -> event

( Close the window )
win-id CLOSE-WINDOW -> status
```

### Word Definitions

All SCI words are defined in `lib/sci.lux` and follow a consistent pattern:

```lux
: CREATE-WIN ( name-ptr size -- win-id )
    SCI-CREATE-WIN ROT SCI-EXEC
;
```

The `SCI-EXEC` helper word handles the port writing sequence:
1. Write arg2 to SCI-ARG2
2. Write arg1 to SCI-ARG1
3. Write cmd to SCI-CMD
4. Read result from SCI-PORT

## Integration with Services

The SCI commands integrate seamlessly with the OS service architecture:

- **Window commands** → ServiceManager (thread-safe window operations)
- **Input commands** → InputManager (event polling from buffered queue)
- **File commands** → FileSystemManager (sandbox-enforced file operations)
- **Sound commands** → SoundServer (audio playback)
- **Process commands** → System (direct operations)

## Safety and Sandboxing

File operations are sandboxed through the resolver pattern:
- All file paths are resolved through `System.resolvePath()`
- Paths that escape the sandbox root are rejected
- Symlinks are followed and checked against the sandbox boundary

Memory access:
- String pointers (for paths, window names) are validated against memory bounds
- Out-of-bounds reads result in empty strings or errors
- The VM's memory is trusted (internal code boundary)

## Future Extensions

### Drawing Primitives
SCI-DRAW-RECT and SCI-DRAW-TEXT can be extended to support:
- Line drawing
- Polygon filling
- Text rendering with various fonts
- Sprite blitting

### Advanced File I/O
File operations can be extended for:
- Asynchronous I/O with completion callbacks
- Directory listing and traversal
- File permissions and metadata operations

### Process Management
Process control can be extended for:
- Process forking
- Pipe creation and management
- Signal handling

### Memory Management
Could add:
- Dynamic memory allocation (malloc/free)
- Heap debugging and profiling
