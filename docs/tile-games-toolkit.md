# Tile-Based Games Toolkit

## Context

The user wants a reusable toolkit for building simple tile-based games on
top of NUX/Cloister. Today the only tile-based game is `apps/Snake.lux`,
which hand-rolls everything a tile game needs — grid↔pixel layout, a
tile-cell painter, a ring-buffer entity list — directly in its own code,
using addresses it picked itself. There is no shared library any future
game (Sokoban, Minesweeper, a maze, etc.) could reuse, and there's no way
to draw a real multi-color tile: `/dev/draw`'s only per-cell primitives are
solid `fill-rect`, one of 4 dither patterns (`fill-pat`), and 1-bit CFF
glyph blits (cmd 9) — no true bitmap sprite.

Per the user's choices: this plan (a) adds a genuine multi-color bitmap
tile-blit primitive at the VFS/C layer (mirroring how CFF glyph blit — cmd
9 — already works), and (b) extracts Snake's grid/layout/cell-paint pattern
into a new shared library, then refactors Snake to use it as the first
consumer/proof.

Constraints from `AGENTS.md`: never add a VM opcode (a new `/dev/draw`
wire command is fine — that's how cmd 0-9 already work, all via `STOREI`/
`VFS::write`, no opcode involved); Cloister stays single-app; visual style
stays System-6-ish (this toolkit doesn't change that — tiles are an
additional drawing primitive, not a new UI paradigm).

## 1. New VFS draw command 10: BlitTile (C side)

Add a **true multi-color bitmap tile blit**, following cmd 9's
(`DrawCFFGlyph`) exact division of responsibility: `vfs.c` validates the
guest pointer/length and hands a raw pointer into `system.c`, which trusts
it and just draws.

**Wire format** (`case 10` in `draw_write`, `src/vfs.c`, added after the
existing `case 9` block at line ~1026):

> **Superseded — the byte layout below is historical.** `/dev/draw` has since
> moved to a word-aligned format: every command is a sequence of 32-bit VM
> words (big-endian, as `write_mem32` writes them), one word per field, so
> guests pack commands with plain `STOREI` stores. Byte-packing every field
> cost a load/mask/shift/store sequence per byte — byte access is not an
> opcode — which measured ~44% of a full Easel repaint. Cmd 10 is now 8 words
> (32 bytes): `cmd, x, y, size, use_key, key, tile_ptr, nbytes`. The field
> order and meanings are unchanged; only the encoding is. See the comment
> above `draw_write` in `src/vfs.c` for the current contract.

```
[0]  cmd = 10
[1-2]  x   i16 LE
[3-4]  y   i16 LE
[5]    size u8       ( tile is size×size pixels, e.g. 8/16/24/32 )
[6]    use_key u8    ( 1 = key color is transparent, 0 = opaque blit )
[7-10] key   u32 LE  ( RGB, top byte ignored — matches existing color encoding )
[11-14] tile_ptr u32 LE  ( guest address of pixel data )
[15-16] nbytes  u16 LE   ( bytes available at tile_ptr; must be >= size*size*3 )
```
Total 17 bytes, same shape as cmd 9. Pixel data at `tile_ptr` is
`size*size` pixels, row-major, **3 bytes/pixel RGB** (no alpha — matches
the framebuffer's internal convention where alpha is always opaque).

Guest-memory validation lives in `vfs.c` (mirrors cmd 9's clamp at
`src/vfs.c:1017-1024`): reject if `tile_ptr >= sys->memory_size`, clamp
`nbytes` to `sys->memory_size - tile_ptr`, and bail (via `break`, not the
`default` abort-path) if `nbytes < size*size*3`.

**New C function**, declared in `include/system.h` next to
`system_draw_cff` (line 132) and implemented in `src/system.c` near
`system_draw_cff` (lines 740-803, used as the direct template):

```c
void system_draw_tile(System* sys, const uint8_t* pixels, int size,
                       int32_t x, int32_t y, int use_key, uint32_t key);
```

Follow `system_draw_cff`'s conventions exactly: guard `!sys->screen_pixels`
early; write into `sys->back_pixels ? sys->back_pixels : sys->screen_pixels`;
per-pixel clip against `sys->screen_width`/`screen_height`; pixel index
`(py * sw + px) * 4`, bytes `[0]=0xFF alpha, [1]=r, [2]=g, [3]=b`. For each
of the `size*size` source pixels, read 3 bytes RGB from `pixels`, skip the
write if `use_key` and `(r,g,b) == key`, else clip+plot.

**Test**: add `test_draw_tile_cmd10()` to `src/test_vfs.c`, modeled on
`test_draw_cff_cmd9()` (lines 446-484) — build a small RGB tile buffer in
`sys->memory`, write the 17-byte command via `vfs_write`, assert the
expected pixels are set (including a color-keyed transparent corner is
*not* painted). Register it alongside the existing calls in `main()`
(~line 1462-1464).

## 2. Lux-side wrapper: `DRAW::blit-tile`

In `lib/draw.lux`, add a word next to `@cff-glyph` (lines 171-184) using
the same pack-into-`BUF`-then-`VFS::write` idiom:

```
@blit-tile ( fd x y size use-key key tile-ptr nbytes -- )
```

packing the 17-byte wire format above.

## 3. New library: `lib/tilemap.lux`

Generalizes Snake's `@layout`/`@cell-box`/`@cell-solid`/`@cell-pat`
(`apps/Snake.lux:136-229`) into a reusable module, `MODULE TILEMAP`,
`IMPORT DRAW`.

**Design**: the tilemap's *bulk data* (the per-cell tile-ID array, and any
tile bitmap pixel data) is **owned by the calling app**, passed in as
addresses — per `docs/memory-map.md`, bulk buffers belong to individual
apps in the `0x900000-0xA00000` band, not to a shared library. The
library's own *small* per-instance state (cols/rows/cell-px/offx/offy/
map-buf-ptr, tileset table) gets a small fixed slice, like `lib/cff.lux`
and `lib/sf.lux` do.

**Address**: reserve `0x8D1000` (confirmed free — `apps/Calculator.lux` is
the only current occupant of the `0x8D0000` region and stops at
`0x8D0030`). Document this in `docs/memory-map.md`'s app small-state band
row.

**API**:
- `TILEMAP::layout ( cols rows max-w max-h --  )` — computes cell size to
  fit the given cols×rows into a max-w×max-h box (generalized
  `apps/Snake.lux:136-142`'s `@layout`, which hardcoded a square 20×20
  grid); stores cols, rows, tile-px, offx, offy.
- `TILEMAP::cols ( -- n )`, `TILEMAP::rows ( -- n )`, `TILEMAP::tile-px ( -- n )`
- `TILEMAP::cell-box ( gx gy -- x y w h )` — direct generalization of
  Snake's `@cell-box` (lines 212-219).
- `TILEMAP::pixel-to-cell ( px py -- gx gy valid? )` — inverse mapping for
  mouse picking (new; Snake never needed this since it has no mouse-driven
  board interaction, but Sokoban/Minesweeper-style games will).
- `TILEMAP::map-use ( buf-addr --  )` — tell the library where the caller's
  cols*rows byte array of tile-IDs lives.
- `TILEMAP::get ( gx gy -- tile-id )`, `TILEMAP::set ( gx gy tile-id -- )`
- Tileset registry (up to e.g. 32 kinds), each tile-ID mapped to one of:
  - `TILEMAP::kind-color ( id color -- )` — solid `fill-rect`.
  - `TILEMAP::kind-pattern ( id color pat -- )` — `fill-pat` (Snake's body/
    food cells today).
  - `TILEMAP::kind-bitmap ( id tile-ptr nbytes use-key key -- )` — new
    `DRAW::blit-tile` path from part 1/2.
- `TILEMAP::render ( fd --  )` — iterates the map buffer, cell by cell,
  and dispatches to the right `DRAW::` call per the tile's registered
  kind. This is the generalized `apps/Snake.lux:242-268` (`@paint-field`),
  minus the body-specific ring-buffer walk (which stays game-specific,
  see below — Snake's snake-body isn't "tilemap" data, it's a separate
  entity list drawn *over* the board).

**Entity list helper** (same file or a small `TILEMAP::` section): extract
Snake's ring-buffer segment storage (`apps/Snake.lux:42-62`, `@wrap`/
`@seg-addr`/`@get-seg`/`@set-seg`) into generic words operating on a
caller-supplied buffer + capacity, e.g. `TILEMAP::ent-addr ( i cap buf -- addr )`,
`TILEMAP::ent-get`/`TILEMAP::ent-set` — useful for any game with a moving
chain/queue of grid positions, not just Snake.

## 4. Update `docs/memory-map.md`

Add `lib/tilemap.lux` to the app small-state band's occupant list
(`0x800000-0x900000` row) noting its `0x8D1000` slice, same style as the
existing row's occupant list.

## 5. Refactor `apps/Snake.lux` onto the toolkit

Replace the hand-rolled layout/cell-paint code with `TILEMAP::` calls:
- `INCLUDE "lib/tilemap.lux"`, `IMPORT TILEMAP`.
- `@layout` → `TILEMAP::layout` call (still need `BAR_H` accounted for in
  the max-height argument, same as today).
- `@cell-box`/`@cell-solid`/`@cell-pat` → replaced by direct calls to
  `TILEMAP::cell-box` and `DRAW::fill-rect`/`DRAW::fill-pat` (Snake's board
  itself doesn't need a persisted tile-ID array/`TILEMAP::render` — the
  board is just an empty frame; only the snake body + food need per-cell
  painting, and those are entity-like, drawn via `TILEMAP::cell-box`
  directly). This keeps the diff honest: Snake proves out the
  layout/cell-box/entity-list pieces of the toolkit; a *second* game with
  static per-cell tile types (e.g. a maze/board with walls) would be the
  one to exercise `TILEMAP::render`/tileset registry end-to-end — out of
  scope for this plan per the user's answer (Snake refactor only, no new
  demo game), but the API above is shaped for that with `kind-bitmap`/
  `kind-color` fully wired even though Snake itself only exercises
  `cell-box` + fill-rect/fill-pat.
- `@body`/`@seg-addr`/`@get-seg`/`@set-seg`/`@wrap` → replaced by
  `TILEMAP::ent-*` calls against the same `0x8A1000` buffer Snake already
  owns.
- Everything else (game logic, state machine, `APP::`/`UI::`/`EVENT::`
  usage) stays as-is.

## Verification

1. `make test` — runs `test_vfs` (including the new `test_draw_tile_cmd10`)
   plus the existing suite; must pass.
2. `make apps` (or `make all`) — rebuilds `luxc` and all `apps/*.bin`,
   including the refactored `apps/Snake.bin`, confirming `lib/tilemap.lux`
   compiles cleanly and Snake still compiles against it.
3. Run `./bin/cloister apps/Snake.bin` and play a round: confirm the board
   layout, snake movement/growth, and food rendering look pixel-identical
   to before the refactor (this is a refactor, not a visual change).
4. Manually exercise the new bitmap path: write a tiny throwaway Lux/
   Fluxio test program (or extend `test_vfs.c`) that calls
   `DRAW::blit-tile`/`system_draw_tile` with a small colorful checkerboard
   tile and a color-keyed transparent pixel, run it under `cloister`, and
   visually confirm both the color-key transparency and opaque paths.
