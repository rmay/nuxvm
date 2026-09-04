# The 16-colour system palette

Cloister draws in colour, but not in arbitrary colour. Every app that opts in
paints from one fixed 16-entry palette, and the host enforces it: under the
`c4` draw channel, any ink an app emits is snapped to the nearest palette
entry before it reaches a pixel. An app cannot go off-palette by accident,
and it does not have to be trusted not to.

This is the authoritative list. It exists in three places that must agree:

| where | what |
|---|---|
| `src/system.c` | `system_palette[16]`, and `palette_snap` which quantizes to it |
| `lib/draw.lux` | `DRAW::PAL_*` constants and the `DRAW::CLUT_TAB` lookup table |
| this file | the human-readable reference |

`test_palette_matches_lux` in `src/test_vfs.c` reads `lib/draw.lux` and
compares all sixteen values against `system_palette_entry()`, so the first
two cannot drift apart silently. If you change a colour, change it in both
and the test will tell you if you missed one.

## The palette

| # | RGB | name | `lib/draw.lux` |
|---|---|---|---|
| 0 | `0xFFFFFF` | white | `DRAW::PAL_WHITE` |
| 1 | `0xAAAAAA` | light gray | `DRAW::PAL_LTGRAY` |
| 2 | `0x555555` | dark gray | `DRAW::PAL_DKGRAY` |
| 3 | `0x000000` | black | `DRAW::PAL_BLACK` |
| 4 | `0xDD0000` | red | `DRAW::PAL_RED` |
| 5 | `0xEE7700` | orange | `DRAW::PAL_ORANGE` |
| 6 | `0x3366EE` | blue | `DRAW::PAL_BLUE` |
| 7 | `0x00CCCC` | cyan | `DRAW::PAL_CYAN` |
| 8 | `0xEEDD00` | yellow | `DRAW::PAL_YELLOW` |
| 9 | `0x007700` | dark green | `DRAW::PAL_DKGREEN` |
| 10 | `0xDD00DD` | magenta | `DRAW::PAL_MAGENTA` |
| 11 | `0x0000CC` | dark blue | `DRAW::PAL_DKBLUE` |
| 12 | `0x770000` | dark red | `DRAW::PAL_DKRED` |
| 13 | `0x770099` | purple | `DRAW::PAL_PURPLE` |
| 14 | `0x996633` | brown | `DRAW::PAL_BROWN` |
| 15 | `0x0077AA` | teal | `DRAW::PAL_TEAL` |

`DRAW::clut ( i -- color )` maps an index to its ink word.

## Two properties the order is chosen for

Neither is cosmetic; both are load-bearing, and changing the order breaks
them.

**Entries 0-3 are the old 2bpp gray ramp, in the same order.** They are
exactly the four `DRAW_CHAN_K2` levels, and exactly `GRAYMAP`'s
`WHITE`/`LT`/`DK`/`BLACK`. That is what makes a 2bpp page widen to a 4bpp
`CMAP` page index-for-index, with no value remapping — so Easel's `EAS3`
files open in the new format looking identical rather than merely similar
(`CMAP::widen-2bpp`).

**Complements sit at `i XOR 3`.** white/black and the two grays pair as they
always did; then red/cyan, orange/blue, yellow/dark blue, green/magenta,
dark red/teal. This is what lets `CMAP::invert` stay a single
`0x33333333` XOR per word — the same cost as `GRAYMAP::invert` at 2bpp —
instead of a per-pixel lookup over 200 KB. purple/brown is the leftover pair
and is a weak complement; that is a deliberate taste call.

## What is deliberately *not* in the palette

`0xFF00FF` — magenta-key. It is the sprite transparency key
(`apps/Whittle.lux`, `DRAW::blit-tile`), so it must stay out of gamut or a
legitimately opaque pixel could quantize into it. The palette's magenta is
`0xDD00DD` instead. `test_draw_chan_c4` asserts this.

## Using it

```
APP::palette!                      ( c4: ink snaps to the palette )
APP::grayscale!                    ( k8: ink collapses to luma -- the old look )
APP::color!                        ( RGB: no quantization at all )

APP::draw-fd x y w h DRAW::PAL_RED DRAW::fill-rect
n DRAW::clut                       ( a 4bpp CMAP pixel value -> ink )
```

UI chrome (`lib/ui.lux`, `lib/menu.lux`) stays black and white on purpose.
The System 6 look is the chrome; colour belongs to what an app draws inside
it.

## Storage

A palette index is four bits, so a page of them is 4bpp — `lib/cmap.lux`, the
sibling of the 1bpp `lib/bitmap.lux` and the 2bpp `lib/graymap.lux`. At
576x720 that is 207,360 bytes a page, twice the 2bpp cost. Going the other
way, it makes bitmap tiles *cheaper*: `DRAW::blit-tile` takes 3 bytes per
pixel, so a 16x16 tile is 768 bytes there against 128 as a 4bpp page.

An 8-colour palette was considered and rejected: 3 bits do not divide into a
byte, so pixel 2 of every byte straddles a boundary and each get/set becomes
a two-byte read-modify-write — expensive in an interpreter where `load-byte`
is already a synthesized 40-80 instruction sequence. Padding 3bpp out to a
nibble would pay 16-colour memory for 8 colours.
