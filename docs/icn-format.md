# ICN format

`lib/icn.lux` — fixed 16×16, 1-bit-per-pixel icon glyphs, System 6 style
(think Finder's small icons, or a MacPaint tool palette). One module covers
three things: the in-memory bitmap layout, an ASCII-art authoring path for
icons baked into an app's source, and an on-disk file format (`ICN1`) so an
icon can be created in one app and consumed by another over the VFS.

`INCLUDE "lib/icn.lux"` then `IMPORT ICN`. Built on `lib/core.lux`,
`lib/draw.lux` (framebuffer blit + `load-word-le`/`store-word-le`), and
`lib/vfs.lux` (`ICN::load`/`save`).

## In-memory layout

An icon is 16 rows × 2 bytes, MSB-first — bit 7 of a row's first byte is
the leftmost pixel, bit 0 of its second byte is the rightmost. Same
row-major, MSB-first convention as `lib/bitmap.lux` and Easel's `BRUSHES`
table, just fixed at 16×16 instead of `BITMAP`'s full-page size or
`BRUSHES`'s per-preset masks.

```
@SIZE      16   ( icon is SIZE x SIZE pixels, always square )
@ROW_BYTES  2   ( SIZE / 8 )
@BYTES     32   ( SIZE * ROW_BYTES -- bytes per icon )
```

Icons live in a caller-owned table: `base + index*BYTES` is icon `index`'s
first byte. There's no header, no per-icon metadata, no mask plane — a 0
bit is simply "don't touch the pixel underneath," so the caller draws
whatever cell background/highlight it wants first and then blits the icon
on top. That's deliberate: every current use (Easel's tool palette) already
erases or fills the cell before painting the icon, and a caller that wants
XOR/invert can just pass a different `color` (see `draw` below).

No mask plane also means there is no "1-bit ink, but which of the pixels
that aren't ink are actually transparent vs. white" ambiguity System 6's
real `ICN#`/`ics#` had to solve with a second bitmap — this format doesn't
have opaque-white icons, only ink-on-whatever's-there-already.

## API

| Word | Stack | Does |
|---|---|---|
| `ICN::SIZE` / `ROW_BYTES` / `BYTES` | `-- n` | the constants above |
| `ICN::addr` | `base index row -- addr` | address of row `row` of icon `index` |
| `ICN::get-bit` | `base index col row -- b` | 0 or 1 |
| `ICN::set-bit` | `base index col row v -- ` | write one bit |
| `ICN::clear` | `base index -- ` | zero one icon (all bits 0) |
| `ICN::row!` | `base index row text -- ` | paint one row from ASCII art (below) |
| `ICN::draw` | `fd base index x y color -- ` | blit icon `index` to the framebuffer at `(x,y)` |
| `ICN::load` | `fd base max-count -- count` | read an `ICN1` file into `base`; see below |
| `ICN::save` | `fd base count -- ok` | write `count` icons from `base` as an `ICN1` file |

`get-bit`/`set-bit` are the pixel-editor primitives — an icon editor UI
(mouse down on a 16×16 zoomed grid) is just `set-bit` per cell. `draw` is
the fast path used every frame: it doesn't call `get-bit` at all, since
that would recompute `index*BYTES` and re-run the row/col arithmetic once
per pixel. Instead it computes the icon's base address once, loads each
row's 2 bytes once, and run-length-encodes each row into `DRAW::hline`
calls (transparent gaps just aren't drawn) — cheap enough to call for every
icon on every redraw of a palette-sized set.

## Authoring in source: ASCII art

```lux
@init-tool-icons ( -- )
    TOOL_ICONS 0 ICN::clear      ( icon index 0 in the TOOL_ICONS table )
    TOOL_ICONS 0 1  T"....######......" ICN::row!
    TOOL_ICONS 0 2  T"..##......##...." ICN::row!
    TOOL_ICONS 0 3  T".#..........#..." ICN::row!
    ...
;
```

`row!` reads a null-terminated string up to `SIZE` characters: `#` sets the
bit, anything else (`.` by convention, for readability) leaves it clear.
The row string can be shorter than 16 characters — `row!` stops at the
terminator rather than reading past it, so a mostly-blank row can just be a
short string instead of 16 dots. This is how Easel's `apps/Easel.lux`
(`init-tool-icons`) draws its 20 tool-palette icons: no build step, no
external file, the pixel art sits directly next to the tool it's for and a
reviewer can see the shape in a diff.

Use this path when an icon is fixed, versioned with the app, and small in
count — exactly what a tool palette is.

## On-disk: the `ICN1` file format

Use this path when icons need to move between apps or be user-editable at
runtime (an icon editor saving/loading, a document embedding a custom
icon set) rather than being baked into source. An `ICN1` file is an 8-byte
header followed by `count` icons, back to back, in the same `BYTES`-per-icon
layout described above:

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | ASCII `"ICN1"` |
| 4 | 2 | count | icon count, little-endian u16 |
| 6 | 1 | size | pixel width/height of each icon (currently always 16) |
| 7 | 1 | reserved | write 0; ignore on read |
| 8 | `count * BYTES` | data | icons back to back, row-major MSB-first |

`size` exists so a reader can refuse a file it can't handle rather than
silently misinterpreting the data — today's `lib/icn.lux` only supports
`SIZE = 16` and `ICN::load` checks `size` against `ICN::SIZE`, returning
`0` (no icons loaded) on any header mismatch, same as a bad magic.

```lux
( write -- mode 6 = write/create, same as Easel's save-file )
T"/sys/file/tools.icn" make-vfs-path 6 VFS::open-mode { fd }
    fd 0 >= [
        fd TOOL_ICONS TOOLS ICN::save drop
        fd VFS::close drop
    ] ?
UNGIRD

( read, up to 32 icons -- mode 0 = read, same as Easel's load-path )
T"/sys/file/tools.icn" make-vfs-path 0 VFS::open-mode { fd }
    fd 0 >= [
        fd MY_ICONS 32 ICN::load { n }
            ( n icons now sit at MY_ICONS, MY_ICONS+BYTES, ... )
        UNGIRD
        fd VFS::close drop
    ] ?
UNGIRD
```

`fd` is a file already opened by the caller — `load`/`save` only move
bytes, matching the convention `lib/cff.lux`'s `CFF::load`/`save` and both
Easel's (`save-file`/`load-path`) and Whittle's (`apps/Whittle.lux:475`)
own EAS2/CSF reader-writers already use: `make-vfs-path` here is each
app's own `/sys/file/`-prefixing helper (not a `lib/vfs.lux` word), open
via `VFS::open-mode`, pass the fd in, close it after. There's no multi-file
resource fork here, no name-per-icon — an `ICN1` file is just an ordered
list; whoever writes it and whoever reads it need to agree on what index
`N` means (Easel's tool icons agree by using the `TOOL_*` constants as the
index, whether the source is `row!` or a loaded file).

## Using it from Whittle

Whittle (`apps/Whittle.lux`) is currently a 32×32 **RGB** tile/sprite editor
(`SLOT_BYTES` 3072 = 32×32×3, `DRAW::blit-tile`, saved as CSF) — a
different pixel format and a different file format from ICN, so it can't
just point its existing sprite-frame code at an `.icn` file. To let Whittle
create or edit ICN icons, it needs its own small mode/tool built on this
module directly, the same way Easel's tool palette is:

- **Canvas**: a 16×16 (or zoomed, e.g. 16px-per-cell for a 256×256 editing
  area) grid backed by one `ICN::BYTES`-sized icon slot. Mouse-down calls
  `ICN::set-bit`; painting is just toggling bits, no color picker needed
  (1-bit, ink or nothing).
- **Multiple icons per document**: a table of `N * ICN::BYTES`, same shape
  as Easel's `TOOL_ICONS`, with whatever selection/thumbnail-strip UI
  Whittle already has for sprites (its frame strip is the same idea one
  size class up).
- **Load/Save**: `ICN::load`/`ICN::save` against a `.icn` path, parallel to
  Whittle's existing CSF save/load — pick a document extension (`.icn`)
  distinct from CSF's so File > Open can tell them apart, or add an
  explicit New Icon Set vs. New Sprite Sheet choice if the two live in one
  document type.
- **Rendering a preview**: `ICN::draw` blits straight to the framebuffer at
  1x; for a zoomed editing canvas, Whittle needs its own zoomed painter
  (walk `ICN::get-bit` over the 16×16 grid and `DRAW::fill-rect` an N×N
  block per bit — `get-bit`'s per-pixel cost, fine at edit-time scale,
  is exactly what `ICN::draw` avoids for repeated whole-palette redraws).

None of this requires changes to `lib/icn.lux` itself — `get-bit`/`set-bit`
already are the pixel-editor primitives, and `load`/`save` already make a
set of icons a portable file. What's missing is only Whittle-side UI: a
tool/document mode that owns an icon table and drives it through this API,
plus a `.icn` File > Open/Save path.
