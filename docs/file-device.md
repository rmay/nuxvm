# File Device

The File device is CLOISTER's bridge to the host filesystem. It lives at MMIO
port `0x3060` (4 × 32-bit registers, 16 bytes total) and is modelled on
Varvara's File device, adapted to nux's word-aligned register style.

## Sandbox

CLOISTER captures its launch directory at startup and pins the File device to
it. Every filename supplied through the device is resolved against that root
and rejected with a `-1` result if it escapes via any of:

- an absolute path (`/etc/passwd`)
- parent-directory traversal (`../outside`)
- a symlink whose target lives outside the root

The sandbox root is canonical (symlinks resolved). `NewSystem` defaults the
root to the process `cwd` so unit tests and ad-hoc use stay ergonomic; the
`cloister` binary always pins it explicitly.

## Port Layout

| Offset | Name     | R/W | Meaning |
|--------|----------|-----|---------|
| +0     | `vector` | r/w | Vector address (reserved for async completion; not yet fired) |
| +4     | `name`   | r/w | Pointer to a null-terminated filename in VM memory. **Writing closes the open handle and resets the cursor.** |
| +8     | `buffer` | r/w | Pointer to a data buffer in VM memory |
| +12    | `cmd` / `result` | r/w | Write: packed command word. Read: last op's result (bytes transferred, file size, or `-1`). |

## Command Word (+12 write)

```
 31          24 23          16 15               0
+--------------+--------------+-------------------+
|    command   |    flags     |      length       |
+--------------+--------------+-------------------+
```

| Command | Code | Semantics |
|---------|------|-----------|
| READ    | `0x01` | Read up to `length` bytes from the current cursor into `buffer`. Cursor advances. If `name` points at a directory, each call emits one entry line. Result: bytes written (0 = EOF / end of listing, -1 = error). |
| WRITE   | `0x02` | Write `length` bytes from `buffer` to the current file. The first WRITE after a `name!` truncates unless the append flag (`flags & 1`) is set, in which case the file is opened in append mode. Subsequent WRITEs continue sequentially. Result: bytes written, or -1. |
| STAT    | `0x03` | If `length ≥ 4` and `buffer` is non-zero, writes a Varvara-style 4-char detail string (`"0042"` for a 66-byte file, `"----"` for a directory, `"????"` for files >64 KiB). Result: file size, or -1. |
| DELETE  | `0x04` | Remove the named file. Result: 0 on success, -1 on failure. |
| SEEK    | `0x05` | Close any open handle and reset the cursor to 0. Result: 0. |

## Cursor lifecycle

- Writing a new pointer to `name` (`+4`) closes both the read and write
  handles and clears all cursor state.
- The first READ on a name decides whether it is a file or a directory and
  opens the appropriate resource lazily.
- READ and WRITE each advance the cursor, so streaming a large file across
  many small buffers is a matter of repeated READs.
- SEEK rewinds and drops any open handles.
- DELETE always closes handles before removing the file.

## Directory listing format

When `name` points at a directory, each READ writes a single line:

```
xxxx filename\n
```

where `xxxx` is:

- the 4-digit lowercase hex size for a regular file (`0042`)
- `????` for a regular file larger than 65535 bytes
- `----` for a sub-directory
- `!!!!` if the entry's metadata couldn't be read

If the buffer is smaller than the line, the line is truncated to fit; the
result is the number of bytes actually written.

## Lux module

`lib/system.lux` exposes the device as `MODULE FILE`:

```lux
MODULE FILE
@name!   ( ptr -- )
@buf!    ( ptr -- )
@read    ( len -- res )
@write   ( len -- res )
@append! ( len -- res )   ( WRITE with append flag set )
@size    ( -- size )      ( STAT, returns size )
@delete  ( -- res )
@seek    ( -- res )
@list    ( len -- res )   ( alias for @read, documents intent )
```

## Example: write, seek, read, list, delete

```lux
( Write "hello" to greet.txt )
greet-path FILE::name!
greet-buf  FILE::buf!
5 FILE::write

( Re-read it. Writing the name again closes the writer and rewinds. )
greet-path FILE::name!
read-buf   FILE::buf!
5 FILE::read

( List the current directory )
dot-path   FILE::name!
line-buf   FILE::buf!
[ 64 FILE::list 0 > ] WHILE ...

( Tidy up )
greet-path FILE::name!
FILE::delete
```
