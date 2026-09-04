# A fixed 16-color system palette for Cloister

## Context

Easel's document page went 1bpp → 2bpp gray in 59f28bd (`lib/graymap.lux`,
`EAS3`), and the machine now wants color — but color that stays inside a
budget, on a System 6 machine, without abandoning the discipline that makes
the look coherent.

The audit found that **color is not a rendering problem here**. The
framebuffer is already 32bpp ARGB (`src/system.c:405`, aliased at guest
`0x100000`), SDL presents it as `ABGR8888` (`src/cloister.c:426`), and
every `/dev/draw` geometry command already carries a full 24-bit ink word
(`lib/draw.lux:155-232`). Grayscale is a *quantizer applied at draw time*:
`system_map_color` (`src/system.c:410-426`) collapses ink to Rec. 601 luma
whenever `draw_chan != DRAW_CHAN_RGB`, and 12 of 14 apps opt into it with a
single `APP::grayscale!` call. `APP::color!` for unrestricted RGB already
exists at `lib/app.lux:364` and works today.

So there are only two things actually worth building:

1. **A quantizer that enforces the palette.** A fifth draw channel that
   snaps any ink to the nearest of 16 fixed system colors — the same
   mechanism as k1/k2/k8, one new value in an existing command. This is
   what keeps the look coherent without trusting every call site.
2. **A 4bpp storage format.** This is the only part with a memory cost, and
   it lands almost entirely in Easel's page buffers.

**Depth: 16 colors, 4bpp.** A nibble is 2 px/byte and 8 px/word, so the
addressing in `lib/graymap.lux` (`x 4 /`, `3 x 3 AND - 2 *`) carries over
with only the divisor and shift changed. 8 colors at 3bpp does not divide
into a byte — pixel 2 straddles a byte boundary, turning every get/set into
a two-byte read-modify-write in an interpreter where `load-byte` is already
40-80 instructions; and padding 3bpp to a nibble pays 16-color memory for 8
colors. 4 colors is free but the System 6 chrome alone (white, black, two
grays) consumes the whole table.

| surface | today | after | delta |
|---|---|---|---|
| Easel `CANVAS`/`UNDO`/`TMP`/`PACK` (`apps/Easel.lux:252-256`) | 414,728 B | 829,448 B | **+414,720 B** |
| a 16×16 bitmap tile (`DRAW::blit-tile`, 3 bytes/px) | 768 B | 128 B | **−640 B** |
| framebuffer, wire, `BATCH_BUF` | — | unchanged | 0 |

Easel's RESERVE total goes from ≈614 KB to ≈1.03 MB, comfortably inside the
3 MB Lux RESERVE band (`0xA00000–0xD00000`). Nothing here grows the draw
wire, so this does **not** collide with `docs/memory_improvement_plan.md`,
which needs `DRAW::BATCH_BUF` cut to 4 KB for the 64 KB Snake build. Snake
is deliberately out of scope.

## The palette

Fixed in the host, not app-settable. Indices 0-3 are **exactly** the
current `DRAW_CHAN_K2` levels in `GRAYMAP`'s order (0 = white … 3 = black),
so existing 2bpp pages widen to 4bpp losslessly with no value remapping.

```
 0 0xFFFFFF white      4 0xDD0000 red        8 0x007700 dk green   12 0x0000CC dk blue
 1 0xAAAAAA lt gray    5 0x770000 dk red     9 0x00CCCC cyan       13 0xDD00DD magenta
 2 0x555555 dk gray    6 0xEE7700 orange    10 0x0077AA teal       14 0x770099 purple
 3 0x000000 black      7 0xEEDD00 yellow    11 0x3366EE blue       15 0x996633 brown
```

Magenta is `0xDD00DD`, **not** `0xFF00FF` — that value is Whittle's sprite
transparency key (`apps/Whittle.lux:27`) and must stay outside the gamut.

## Work

### W1 — `DRAW_CHAN_C4` in the host

- `include/system.h:29-33` — add `#define DRAW_CHAN_C4 4`. Widen the
  `draw_chan` range check at `src/vfs.c:1083` from `DRAW_CHAN_K1` to
  `DRAW_CHAN_C4`. **No new draw command, no new opcode** — cmd 11 SetChan
  already carries the value, and the wire format is untouched.
- `src/system.c` — a file-scope `static const uint32_t
  system_palette[16]`, plus a `DRAW_CHAN_C4` branch in `system_map_color`
  (`:410-426`) that returns the nearest entry by squared RGB distance.
  Exact palette values pass through unchanged by construction, so apps that
  name colors correctly cost nothing; sloppy ones get snapped.
- Expose it: `uint32_t system_palette_entry(int i)` in `include/system.h`,
  so tests can pin the table without reaching into the file.
- The six existing `system_map_color` call sites (`system_fill_rect:434`,
  `fill_pat:472`, `draw_char:581`, `set_pixel:733`, `draw_cff:775`,
  `draw_tile:838`) pick this up for free. Tile keying already compares
  pre-map (`src/system.c:834-837`), so transparency is unaffected.
- Fluxio needs nothing: its `set_chan` builtin
  (`src/fluxio_codegen.c:1267-1281`) already takes an arbitrary int.

### W2 — `lib/draw.lux` + `lib/app.lux`

- `@CHAN_C4 4 ;` beside the existing channel constants (`:263-266`).
- Sixteen `@C_WHITE`/`@C_LTGRAY`/…/`@C_BROWN` constants holding the RGB
  words above, and `@clut ( i -- color )` mapping a 4bpp pixel value to
  ink via a `RESERVE`d 64-byte table. Keep `@gray` (`:282`) — k8 apps
  still use it.
- `lib/app.lux:363-364` — add `@palette! ( -- ) draw-fd DRAW::CHAN_C4
  DRAW::chan! ;` alongside `grayscale!` and `color!`.

The table is now duplicated in C and Lux. W6 pins them together with a test
rather than trusting the comment; this is the same discipline the three
wire-format encoders already live under.

### W3 — `lib/cmap.lux`, a 4bpp sibling of `lib/graymap.lux`

New file, modeled line-for-line on `lib/graymap.lux` (199 L). Do **not**
parameterize graymap over bit depth — its speed comes from constant-folded
shifts, and both formats need to coexist anyway (`EAS3` load path).

```
PAGE_W 576   PAGE_H 720   ROW_BYTES 288   PAGE_BYTES 207360   PPW 8
addr:      base y ROW_BYTES * + x 2 / +
shift:     1 x 1 AND - 4 *          ( even x -> high nibble, MSB-first )
pix-mask:  15 28 i 4 * - LSHIFT
rep-word:  v -> v | v<<4, then <<8, then <<16
```

Port the full word set: `get`, `set`, `word@`, `word!`, `qr16`→`qr8`,
`hspan`, `pat-cmap-word`, `hspan-pat`, `fill`, `copy`, `invert`.

**One real semantic decision:** `GRAYMAP::invert` (`:189`) complements the
2-bit level. For 16 colors, define invert as a 16-entry complement table
(index → its visual opposite, grays inverting as today) rather than `15 -
n`, which would scramble hues. Easel's Invert menu item is the only caller.

### W4 — Easel: 2bpp page → 4bpp page

`apps/Easel.lux` is the bulk of this work.

- `apps/Easel.lux:252-256` — `CANVAS`, `UNDO`, `TMP` → 207,360; `PACK` →
  207,368. Swap `IMPORT GRAYMAP` for `IMPORT CMAP` throughout; `lib/bitmap.lux`
  stays for `SEL_MASK`/`VISIT`/`BRUSHES` (1bpp masks, still correct).
- Ink widens from 0..3 to 0..15 (`:359`). `@ink-rgb` (`:407`) becomes
  `ink LOADI CMAP::clut`; the five gray conversion sites (`:407, 2151,
  3489, 3660, 3698`) follow.
- The 2×2 gray picker in the pattern strip (`:3479-3512`) becomes a 16-swatch
  grid; update the hit-test at `:4102-4110` to match.
- Startup: `APP::grayscale!` (`:4477`) → `APP::palette!`.
- **File format `EAS3` → `EAS4`** (`:2702-2721`, `PACK_BYTES` at `:99`).
  Keep the `EAS3` read path: old pages widen index-for-index because
  CLUT[0..3] *is* the old gray ramp. Write `EAS4` only.
- `SNARF_BUF 32768` (`:265`) — its comment about a "~103 KB" full-page 2bpp
  selection is now ~207 KB. Update the comment; raise the cap only if
  clipboard tests show truncation.

### W5 — Breakout and Road Escape

Games only; **Snake stays as it is**, and UI chrome (`lib/ui.lux:14-24`,
`lib/menu.lux:10`) stays black-and-white — that is the System 6 look and it
should not acquire color here.

- `apps/Breakout.lux` — `@brick-ink` (`:389`) `v 1 - 36 * DRAW::gray`
  becomes a lookup into a short palette ramp by hit strength;
  `APP::grayscale!` (`:523`) → `APP::palette!`.
- `apps/RoadEscape.lux` — the named shades at `:554-560` (`GRASS 176`,
  `TAR 48`, `WHITE 255`, `INK 0`) and the four inline `DRAW::gray` calls
  (`:654, 656, 671, 703`) map onto the palette: grass → dk green, tar → dk
  gray, and the civilian/enemy car distinction at `:703` becomes two
  distinct hues rather than two luma steps — which is the whole point, since
  the comment at `:650-653` is already reasoning about gray contrast.
  `APP::grayscale!` (`:874`) → `APP::palette!`.

### W6 — Tests and docs

- `src/test_vfs.c` — new `test_draw_chan_c4` beside `test_draw_chan`
  (`:320-368`): an exact palette ink passes through untouched, an
  off-palette ink snaps to the expected entry, and the tile transparency
  key still survives under C4 (mirror `test_draw_tile_key_under_k8:371`).
  Existing k1/k2/k8 assertions must stay green — they are the regression
  net for `system_map_color`.
- **Palette cross-check:** a test that compiles a small Lux program emitting
  all 16 `DRAW::C_*` constants at `CHAN_RGB` and asserts each pixel equals
  `system_palette_entry(i)`. This is what stops the two tables drifting.
- `src/test_compiler.c:4692-4701` — `easel_bit_set()` decodes the EAS3 body
  as 2bpp (`ROW_BYTES=144`, `(3-(col%4))*2`). Rewrite for 4bpp
  (`ROW_BYTES=288`, `(1-(col%2))*4`); its callers at `:4645, 4704, 4754,
  4822-4859` come along unchanged.
- `QUILL_HEX_CARET_LUMA` (`src/test_compiler.c:2503`,
  `src/test_fluxio_compiler.c:3308`) is **not** affected — Quill keeps k8.
  Note this explicitly so nobody "fixes" it.
- Docs: `docs/ui.md` (`:103` Easel page description, `:158-162` the
  draw/graymap/app rows), `ARCHITECTURE.md:243` (cmd 11 channel values),
  `docs/memory-map.md`, and a new `docs/palette.md` holding the authoritative
  16 values. While in `ARCHITECTURE.md`, fix **`:225`, which claims the draw
  wire is little-endian — it is big-endian** (`src/vfs.c:998-1017`).
- **Kelvin:** `/dev/draw` is app-layer, not the 300K VM contract, and no
  opcode is added. But a C4 ROM will not render on a pre-change host, so the
  everything-else collective cooled **400K → 399K** and Easel, separately
  specified, cooled **500K → 499K** (rule 5: its foundation moved, and its
  document format changed). Nux stays at 300K. Recorded in `AGENTS.md`'s
  cooldown log.

## Verification

1. `make && make test` green, including the k1/k2/k8 assertions in
   `src/test_vfs.c` and the new C4 and palette cross-check tests.
2. **Byte-identical regression for untouched apps.** Every app except
   Easel/Breakout/RoadEscape still calls `APP::grayscale!`; render each
   `apps/*.bin` headless to PPM before and after and require identical
   output — the harness from `project_easel_tooltip_redraw_fix`. This is the
   check that catches an accidental change to the shared map path.
3. **Easel format round-trip.** Open a pre-change `.eas` (EAS3) and confirm
   it widens correctly; save as EAS4, reopen, compare pages byte-for-byte.
   Keep one EAS3 fixture in the repo permanently.
4. **Easel by hand in Cloister.** Draw with several inks, marquee-move,
   lasso, copy/paste, undo, invert, Show Page. Undo across an ink change is
   the likeliest place a half-ported 4bpp span routine shows up.
5. **Games by eye.** `./bin/cloister apps/Breakout.lux` and
   `apps/RoadEscape.lux` — brick ramp reads as distinct colors, and civilian
   vs. enemy cars are distinguishable at speed.
6. **Report the numbers.** Final Easel RESERVE total and the resulting top
   of the `0xA00000` band, to confirm headroom.

## Risks

- **Silent quantization.** C4 snapping means a wrong ink renders as a
  plausible neighbor rather than failing. The palette cross-check test is
  the only thing that makes drift visible; do not skip it.
- **Two palette tables.** C and Lux both hold the 16 values, exactly the
  hazard the three wire-format encoders already have. Test-pinned, not
  comment-pinned.
- **`easel_bit_set` blast radius.** Four test groups decode the page through
  it. A wrong nibble order there fails as "selection is off by one pixel,"
  which reads like a marquee bug rather than a decoder bug.
- **Shared test helpers.** `quill_lux_*` are used by Easel, Tabula and
  Whittle too — enumerate callers before any bulk edit.
- **`invert` semantics.** The complement table is a taste call that will
  show up immediately in Easel's Invert; settle it before porting the page
  routines, not after.
