# Architecture

NUX is a 32-bit stack VM. Lux and Fluxio compile to the same 55 opcodes.
Cloister is the graphical host: a Varvara/uxn-shaped fantasy machine, not a
multi-app OS. One ROM owns the 960×720 screen. I/O is Plan 9 VFS files, not
Varvara MMIO device ports. Apps look like Macintosh System 6.

This document describes the system as it exists. Invariants that must not
drift live in `AGENTS.md`. Address bands live in `include/memory_map.h`.
The Lux↔Fluxio calling contract lives in `abi/nux-abi.json`.

## Constraints

1. **Never add opcodes.** The ISA is 55 instructions (`include/opcodes.h`).
   New capability goes through VFS files and `/dev/draw` command bytes, not
   new VM ops.
2. **One program owns the screen.** Cloister is uxnemu, not rio. Do not add
   a window manager or a multi-app shell.
3. **I/O is Plan 9 VFS.** Guests open `/dev/draw`, `/dev/mouse`, `/dev/kbd`,
   `/dev/time`, `/sys/file/…`. There are no Varvara device ports and no
   interrupt vectors. SCI (`0x100D0`) is a private trap that implements
   those file operations — not a guest-facing HAL.
4. **Kelvin versioning.** Nux opcodes and VM implementation are **300K**.
   Everything else is **400K** unless marked otherwise. Hotter can run
   colder; the reverse is not guaranteed. See `AGENTS.md`.

Settled choices:

| Question | Decision |
|---|---|
| Guest I/O | Plan 9 VFS files. Varvara MMIO ports are gone |
| Event format | Binary packets, not Plan 9 text lines |
| Namespaces | Per-System mount table; `bind` attaches an fd to a path |
| Concurrency | No `fork` opcode. Child VMs via `/sys/vm/new` for experiments |

## Layers

```
  apps/*.lux          apps/fluxio/*.fx
  lib/*.lux           fluxlink + lib/uisf.bin
           \              /
            luxc        fluxioc
                 \    /
                  .bin
                    |
        ┌───────────┴────────────┐
        │  Machine               │
        │   VM (cpu, stacks)     │
        │   System (VFS, draw,   │
        │           input queues)│
        └───────────┬────────────┘
                    |
         nux (headless)    cloister (SDL2, 960×720)
```

A `Machine` (`include/machine.h`) is a `VM` plus a `System`. The VM fetches
opcodes; the System owns the framebuffer, input queues, sandbox, and VFS.
Hosts (`nux`, `cloister`) create a machine, pump `machine_tick` until YIELD
or HALT, and translate host events into the System's queues.

## Virtual machine

Dual-stack, big-endian bytecode, interpreted (no JIT).

| Resource | Size |
|---|---|
| Data stack | 8192 × int32 |
| Return stack | 1024 × uint32 |
| Locals / frames | 4096 slots; `FRAME` / `UNFRAME` / `LOCALGET` / `LOCALSET` |
| Loop stack | 1024 (`PUSHR` / `POPR` / `PEEKR` / `PEEKR2`) |
| Graphical memory | 32 MB (`cloister` / `nux` graphical path) |
| Headless memory | typically 4 MB |

Execution is confined to a single contiguous image `[image_base, image_end)`.
Jumps, calls, and writes into that range fault; reads (string literals) are
allowed. `OP_YIELD` returns control to the host for one frame. `OP_HALT`
ends the ROM.

The ISA is frozen. Opcode tables: `include/opcodes.h`, `docs/opcodes.md`.

### VFS syscall trap (SCI)

Guest programs never poke device ports. `lib/vfs.lux` and Fluxio's
`vfs_open` / `vfs_read` / `draw_str` builtins issue a syscall through a
small trap in the `0x10000` band:

1. Store command at `0x100D4`, args at `0x100D8` and `0x100DC`, and a
   third arg at `0x10124` when needed.
2. Storing the second arg fires the handler. `LOAD` `0x100D0` (`SCI_PORT`)
   returns the result.

That is the whole DeviceBus: only `0x10000`–`0x11000` is bus-mapped. LOAD
of an old Varvara port (mouse, datetime, controller, …) faults. The
framebuffer is ordinary RAM on the host side; guests draw through
`/dev/draw`. Audio is `/dev/audio`. Time is `/dev/time`. Input is
`/dev/mouse` and `/dev/kbd`.

## Two languages, one bytecode

Both languages compile to the same VM. They differ only in the front end.

**Lux** (`src/lexer.c`, `src/compiler.c`, `bin/luxc`, `bin/luxrepl`) is
Forth-style: postfix, words, modules, quotations, combinators, named locals
(`GIRD` / `UNGIRD` / `{ names }`). This is the native voice of the standard
library and almost every guest app. `nux` and `cloister` compile `.lux` in
process.

**Fluxio** (`src/fluxio_*.c`, `bin/fluxioc`) is C-like: functions, `if` /
`while` / `for`, structs, arrays, bounded `recursive(N)`, snake_case,
required doc comments. There is no Fluxio REPL. `.fx` always goes through
`fluxioc` to a `.bin` first. The picker lists those bins from
`apps/fluxio/`.

Language guides: `docs/using-lux.md`, `docs/using-fluxio.md`.

### Calling convention

Shared, because it is a property of the VM, not of either compiler:

- Arguments push left-to-right. Forth `( a b -- )` is `f(a, b)`.
- `OP_CALL` to an absolute address.
- `OP_FRAME` pops N values into `locals[]`; TOS becomes local 0.
- `OP_UNFRAME` restores the frame pointer and does not touch the data
  stack, so a value pushed before it is the return value.
- Void vs value is a static property of the callee. Fluxio encodes it as
  `extern void` vs `extern int`. Mixing them desyncs the caller's stack
  without a fault.

Proven by `src/test_abi_conformance.c`. Spec: `abi/nux-abi.json`.

### Linking (`fluxlink`)

The VM will only execute one contiguous image, so a Fluxio app that calls
Lux library words cannot be two separately loaded blobs. `bin/fluxlink`
emits **one merged image**:

```
[ Fluxio app at 0x600000 ]
[ zero-filled gap         ]
[ 'NUX1' header + trampoline table at 0x700000, fixed 0x1000 bytes ]
[ Lux library code at 0x701000 ]
```

Trampoline slots are append-only (`abi/uisf.exports.json`). Reorder,
rename, or remove is a link error. An incompatible break would take a new
ABI major version and a new address band, never an overwrite.

`make uilib` builds `lib/uisf.bin` from `lib/sf.lux` (which includes
`lib/ui.lux`) at base `0x701000`. Quill.fx is the first consumer.

## Memory map

`include/memory_map.h` is the source of truth. `docs/memory-map.md` is the
prose map plus the collision history.

| Band | Range | Owner |
|---|---|---|
| Fluxio small globals | `0x001000`–`0x010000` | `fluxio_codegen.c` bump allocator |
| SCI trap | `0x010000`–`0x011000` | VFS syscall registers only |
| Headless code | `0x011000`… | `nux` |
| Shared Lux flags | `0x500000`–`0x510000` | e.g. `lib/log.lux` |
| Graphical code | `0x600000`… | `cloister` |
| ABI library link | `0x700000`–`0x800000` | trampoline + linked Lux lib |
| App small state | `0x800000`–`0x900000` | hand-picked Lux globals |
| App bulk buffers | `0x900000`–`0xA00000` | file buffers, glyphs, tiles |
| `lib/mem.lux` heap | `0xA00000`–`0xC00000` | bump allocator only |
| Fluxio bulk arrays | `0xD00000`–`0x1000000` | large `byte[]` / `int[]` |

Lux still picks addresses by hand. `luxc` warns on duplicate `@NAME 0xHEX`
constants in a compile. Collisions between *different* apps in the small-
state band are inert only because Cloister loads one ROM at a time. They
stop being inert the moment a Fluxio app and a Lux library share an image
— which is exactly what `fluxlink` does. New constants go in the named
band for their size, recorded in `memory_map.h`, not "the next `0x8xxxxx`
that looked free."

## I/O: Plan 9 VFS

Everything a guest needs is a file. Open, read, write, seek, stat, close,
and bind. `/dev/…` is an alias of `/sys/…` except where noted. Each
`System` has its own namespace: `bind` attaches an open fd onto a path in
that mount table. Union mounts (`bind -a` / `bind -b`) are not
implemented.

This is the Plan 9 model with a single-app host. Channels
(`/sys/chan/new`, `/sys/chan/peer`) and `lib/ninep.lux` are the data plane
between cooperating VMs — not a window system.

### Files the graphical apps use

| Path | Role |
|---|---|
| `/dev/draw` | Write command bytes (below). Read returns LE u16 width, height |
| `/dev/mouse` | 8-byte packets: type, button, x, y (`lib/event.lux`) |
| `/dev/kbd` | 8-byte packets: type, key, modifiers |
| `/dev/time` | 16-byte LE snapshot: unix, packed date, packed hms, monotonic ms |
| `/dev/audio` | Write a LE u32 frequency to beep |
| `/sys/snarf` | Clipboard, 64 KB |
| `/sys/launch` | Write a relative path; on HALT, Cloister loads that ROM |
| `/sys/file/…` | Sandboxed host files (no `..`, no absolute paths from guests) |
| `/sys/dir/…` | Directory listing for the file picker |
| `/sys/font/chicago`, `geneva`, `monaco` | CFF font blobs |
| `/sys/dialog` | Host file-dialog result |

Reads on `/dev/mouse` and `/dev/kbd` are non-blocking: empty queue returns
0 bytes. The app `YIELD`s and tries again next frame. There are no
interrupt vectors.

### `/dev/draw` commands

Packed little-endian, written to the draw fd. Lux: `lib/draw.lux`.
Fluxio: builtins in `fluxio_codegen.c`. The host implements them in
`src/vfs.c` → `src/system.c`. Adding a command is allowed; adding an
opcode is not.

| Cmd | Name |
|---|---|
| 0 | FillRect |
| 1 | DrawChar |
| 2 | DrawString |
| 3 | DrawRect |
| 4 | SetFontSize |
| 5 | SetFontId (0/1 Chicago, 2 Geneva, 3 Monaco) |
| 6 | BeginFrame |
| 7 | EndFrame |
| 8 | FillPat (dither) |
| 9 | DrawCFFGlyph |
| 10 | BlitTile (RGB bitmap, optional key color) |

Ovals, roundrects, and lines are spans of FillRect in Lux, not extra
commands. Cmd 10 is the sprite/tile primitive; `lib/tilemap.lux` and
Whittle consume it.

## Cloister

`src/cloister.c`. SDL2 window 960×720, vsync, a small beep device.

Boot:

1. Argument path → load that ROM.
2. Else compile and run `apps/Picker.lux`.

The picker is itself a guest. Choosing an app writes `apps/Quill.lux` (or a
Fluxio `.bin`) to `/sys/launch` and HALTs. Cloister ejects the current
machine and loads the new image at `0x600000`. An empty launch path from
the picker means Quit. An app that HALTs without a launch path returns to
the picker.

Inside an app, Esc is Continue / Restart / Quit (`lib/app.lux`), not a
host key. Restart jumps to `0x600000`. Quit HALTs. About Cloister is a
modal on the picker.

File dialogs (`src/dialog.c`) are a host overlay: while they are open,
Cloister routes input to the dialog instead of the ROM.

`nux` is the same machine without SDL: console `OUT`, no framebuffer.

## Guest programming model

Lux apps include `lib/app.lux` and follow:

```
title APP::init
UI::new
… build controls …
[ handle-mouse ] APP::on-mouse!
[ handle-frame ] APP::on-frame!
APP::loop
```

`APP::loop` opens `/dev/draw`, `/dev/mouse`, `/dev/kbd`, pumps mouse/kbd
packets, calls the registered quotations, draws, and `YIELD`s. The Esc
overlay is inside that loop.

Fluxio apps write the equivalent loop themselves (`begin_frame` /
`poll_mouse` / `poll_kbd` / `end_frame` / `yield`) and call Lux UI words
through `extern` when linked. They must set `APP::win-set!` if they want
the file picker centered, because they never run `APP::init`.

Look: Chicago for menus and buttons. Quill, Tabula, and Nib offer
Font > Chicago / Geneva / Monaco for document text only.

UI toolkit: `lib/ui.lux`, `docs/ui.md`. Named controls, no layout manager,
handlers run after the mouse is done. `lib/sf.lux` is the Standard File
picker.

## VFS extras (not a window system)

These are real VFS facilities. They must not grow into rio.

- **`/sys/vm/new` and child Machines.** A System can spawn up to 16 child
  VMs, bind files into the child's namespace, and tick them. Cloister
  itself never does this: one ROM owns the screen.
- **`/sys/chan/new` + `/sys/chan/peer` + `bind`.** Byte pipes between
  cooperating VMs, then bound into a namespace like any other file.
- **`lib/ninep.lux`.** A 9P2000 subset (version, attach, single-element
  walk, open, read, write, clunk) as a pure Lux library on those channels.
  No host-directory-via-9P mount yet.
- **`/dev/menu`.** Unbound open fails, so a root app keeps its own
  menubar.

Dead SCI window commands (`CMD_CREATE_WIN` and friends in `lib/vfs.lux`)
are stubs. Drawing goes through `/dev/draw`.

## Tools and tests

| Binary | Role |
|---|---|
| `bin/nux` | Headless runner |
| `bin/luxc` | Lux compiler (`-target headless\|graphical`, `-base`, `-symbols`) |
| `bin/luxrepl` | Interactive Lux |
| `bin/fluxioc` | Fluxio compiler |
| `bin/fluxlink` | Merge a Fluxio app with a Lux library image |
| `bin/cloister` | Graphical host |

`make all` builds the tools and every `apps/*.lux` / `apps/fluxio/*.fx`
ROM. `make test` runs VM, VFS, Lux compiler, Fluxio compiler, and ABI
conformance, including headless drives of Quill, Tabula, Illumos, Nib, and
Easel.

## Related documents

- `AGENTS.md` — opcode freeze, single-app rule, Kelvin numbers
- `include/memory_map.h` / `docs/memory-map.md` — address bands
- `abi/nux-abi.json` — Lux↔Fluxio ABI
- `docs/quill_fluxio.md` — how the ABI and Quill.fx were built
- `docs/ui.md` — widget protocol
- `docs/user-manual.md` — Quill, Illumos, Tabula, Nib, Easel
- `docs/tile-games-toolkit.md` — tile blit + `lib/tilemap.lux`
- `docs/NUX_ARCHITECTURE.md` — Lux module resolution (compiler-internal)
