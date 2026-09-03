# Easel → MacPaint

## Context

`apps/Easel.lux` (1548 lines) is documented in `docs/ui.md:102` and `docs/user-manual.md:754` as a
working MacPaint-style painter, but in practice it does not work as one: a selected tool appears to do
nothing, the pattern swatches vanish, and the menu bar feels unresponsive. Reading the code, three
distinct causes account for that, and none of them is a shallow typo — they are consequences of the
`508a74f` "optimizations" pass colliding with a full-window repaint, plus a redraw loop that cannot
finish inside the VM's per-frame cycle budget.

Separately, even when fixed, Easel is only a sketch of MacPaint: 8 tools, no selection, no text, no
line widths, a page with no scrolling, and an Edit menu with three items. The decision taken
is to rebuild it as an actual MacPaint clone, with all four
feature groups (selection, transforms, line widths + brush shapes, text), and to fix the performance
floor by batching `/dev/draw` writes in the guest rather than adding host commands.

This is a large rewrite. Easel roughly doubles in size and gains a shared `lib/bitmap.lux`.

---

## Confirmed diagnosis

**1. Swatches disappear — `apps/Easel.lux:1238` vs `:1082-1097`.**
`draw-all` starts with a full-window `fill-rect` wipe. `draw-patterns` was optimized to run its
16-swatch pass only when `pat-full` is set, which happens exactly twice: `@start` (`:1537`) and
`resume-draw-all` (`:1252`). Every subsequent `draw-all` erases all 16 swatches and repaints only the
previously- and currently-selected cell. Anything that sets dirty — a stroke, a tool click, opening a
menu — blanks the strip.

**2. Menu feels unresponsive, and the first dab of a stroke lands frames late.**
`machine_tick` gives the guest 1,000,000 cycles per host frame (`src/machine.c:46`), and
`system_end_frame`'s memcpy to `screen_pixels` (`src/system.c:634`) is the only thing that makes a
frame visible. `draw-canvas-1x` (`apps/Easel.lux:1155-1176`) run-length-scans all 175,104
byte-per-pixel canvas bytes on every dirty frame — on its own that exceeds the budget, so one logical
repaint spans several host ticks. Every mouse move over an open menu sets dirty
(`apps/Easel.lux:1409`), so menu hover/open latency is tens to hundreds of milliseconds. On top of
that, `snapshot` (`:534`) word-copies 175 KB (43,776 loop iterations) on *every* mouse-down, and
`do-undo` (`:539`) does it three times.

**3. Latent input bugs.**
`erase-btn` is recomputed from every packet including `MOUSE_MOVE` (`apps/Easel.lux:1367`), where the
host hardcodes `btn = 0` (`src/cloister.c:184`). It survives today only because `set-plot-mode` is
called once at mouse-down. Mouse packets also carry no modifiers at all —
`system_push_mouse_event` zeroes them (`src/system.c:669`) and `mouse_read_impl` zeroes bytes 6-7
(`src/vfs.c:525`) — so Shift/Option-drag must be latched from the keyboard stream, which is what
`shift-mod` already does and what Option-drag-to-copy will have to do too.

*Not in scope, flagged:* the uncommitted `--width/--height` work in `src/cloister.c` takes unvalidated
`atoi` values and `system_set_resolution` only reallocs `back_pixels` while `screen_pixels` stays
aliased at `mem[0x100000]` (`src/system.c:377-398`) with no size check. `--width 4000 --height 4000`
writes 64 MB into a 32 MB VM. Worth a separate fix.

---

## Target layout — authentic MacPaint, 512×342

```
0                72                                            512
┌─────────────────────────────────────────────────────────────┐ 0
│ File   Edit   Goodies   Font   Size   Style                 │
├────┬────┬───────────────────────────────────────────────────┤ 20
│ ⌒  │ ▭  │                                                   │
│ ✋ │ A  │                                                   │
│ ▓  │ ✳  │        viewport 440 × 280                         │
│ ●  │ ✎  │        onto a 576 × 720 page                      │
│ /  │ ▤  │                                                   │
│ □  │ ■  │                                                   │
│ ▢  │ ▤  │                                                   │
│ ○  │ ●  │                                                   │
│ ϟ  │ ϟ  │                                                   │
│ ⬠  │ ⬟  │                                                   │
├────┴────┤                                                   │ 240
│ ─────── │                                                   │
│ ─────── │                                                   │
│ ─────── │                                                   │
├─────────┴───────────────────────────────────────────────────┤ 300
│ ███ │ ▒▒ ▓▓ ░░ ▨▨ … 19 across, 2 rows                       │
└─────────────────────────────────────────────────────────────┘ 342
```

- `MENU_H 20` — File / Edit / Goodies / Font / Size / Style.
- Tool palette `x 0..71`, `y 20..239`: 2 cols × 10 rows, cells 36×22. 20 MacPaint tools, in order:
  lasso, marquee | hand, text | bucket, spray | brush, pencil | line, eraser |
  rect, filled rect | roundrect, filled roundrect | oval, filled oval |
  freeform, filled freeform | polygon, filled polygon.
- Line-width strip `x 0..71`, `y 240..299`: 6 rows — none (dotted), 1, 2, 3, 4, 8 px.
- Pattern strip `y 300..341`: current-pattern box at `x 0..35`, then 38 swatches, 19 cols × 2 rows,
  cells 25×20.
- Drawing viewport `x 72..511`, `y 20..299` (440×280) onto a 576×720 page.
- **No in-window status bar.** Document name and dirty `*` go in the host window title via
  `VFS::set-window-title`, which `sync-title` (`apps/Easel.lux:158`) already does.
- `@WIN_W 512 ; @WIN_H 342 ;` — picked up unchanged by `scan_lux_win_size` (`src/cloister.c:38`),
  which accepts anything in `[100, 4000]`. No C change needed.

---


## Step 1 — `lib/bitmap.lux`: 1-bit page primitives

The page moves from **one byte per pixel to packed 1bpp**. This is the load-bearing change and every
later phase depends on it.

Why: 576×720 byte-per-pixel is 414,720 bytes, and `CANVAS + UNDO + selection` is 1.24 MB against a
1 MB `MM_APP_BULK_BUFFER` band (`include/memory_map.h:90-91`). At 1bpp the page is 51,840 bytes, so
five full-page buffers still fit comfortably. It also makes redraw fast — a row is 18 big-endian
words, and an all-white word is skipped in one compare instead of 32 byte loads — and it makes the
on-disk `EAS` format identical to the in-memory one, so save/load become straight `VFS::write`/`read`.

New module `MODULE BITMAP`, addressed off a caller-supplied base so CANVAS / UNDO / selection all
share one implementation:

```
@ROW_BYTES 72 ;   @PAGE_W 576 ;   @PAGE_H 720 ;   @PAGE_BYTES 51840 ;

@addr    ( base x y -- addr )        @mask   ( x -- byte-mask )
@get     ( base x y -- 0|1 )         @set    ( base x y bit -- )
@word@   ( base wx y -- w )          @word!  ( base wx y w -- )
@hspan   ( base x y n bit -- )       ( word-at-a-time interior, edge-masked ends )
@hspan-pat ( base x y n pat-id py -- )
@clear   ( base -- )                 @fill  ( base bit -- )
@copy    ( src dst -- )              @invert ( base -- )
@blit    ( src sx sy dst dx dy w h op -- )   ( op: SRC / OR / XOR / masked )
```

`copy` and `clear` become 12,960-iteration word loops instead of 43,776 — under a tenth of the
current `snapshot` cost, which alone removes the mouse-down stall.

Reuse `lib/geom.lux` (`GEOM::rect-canon`, `rect-intersect`, `rect>xywh`) rather than re-deriving
clipping; Easel's local `@canon-xy` (`apps/Easel.lux:333`) and `@clip-can` (`:721`) go away.

Memory map inside `MM_APP_BULK_BUFFER` (0x900000–0xA00000), all page-sized unless noted:

| Addr | Buffer |
|---|---|
| `0x900000` | `CANVAS` — the page |
| `0x90D000` | `UNDO` |
| `0x91A000` | `SEL_MASK` — 1 where selected (marquee rect or lasso region) |
| `0x927000` | `SEL_BITS` — the floating selection's pixels |
| `0x934000` | `SCRATCH` — transforms, Show Page reduction, trace-edges |
| `0x941000` | `FILL_STK` — 64 KB flood-fill stack (16k entries, was 4 KB) |
| `0x951000` | `PATTERNS` (38 × 8 bytes), `BRUSHES`, `PATH`, `VFS_PATH`, text scratch |

---

## Step 2 — `/dev/draw` batching in `lib/draw.lux`

`draw_write` (`src/vfs.c:986-1124`) already decodes many commands from one buffer —
`while (i < len)` — and `SCI_VFS_WRITE` takes a length in `arg3` with no small cap
(`src/system.c:176-181`). No host change is required; today's cost is purely that every `DRAW::`
word issues its own 13-byte `VFS::write`, i.e. one SCI trap per fill-rect and one per pixel.

Add to `MODULE DRAW`:

```
@batch-begin ( -- )      ( start accumulating into BATCH_BUF )
@batch-flush ( -- )      ( one VFS::write of everything pending )
@batch-on?   ( -- f )
```

with an 8 KB `BATCH_BUF` (≈630 fill-rects per trap) in the small-state band, auto-flushing when a
command would overflow it. `fill-rect`, `draw-rect`, `pix`, `fill-pat`, `draw-char`, `draw-string`
append to the buffer when batching is on and fall back to today's direct write when it is off, so
every existing caller — `lib/ui.lux`, `lib/sf.lux`, the other four apps — is untouched.

Easel then wraps `draw-all` in `batch-begin` / `batch-flush`. Combined with the 1bpp word scan, a full
repaint drops from "several host ticks" to a small fraction of one.

Guard: `DRAW::begin-frame` / `end-frame` must flush before emitting, so a caller cannot leave a
half-built batch straddling a frame boundary.

---

## Step 3 — repaint model

Replace the single `pat-full` flag with an explicit damage set, since the "swatches disappear" bug is
exactly the failure mode of an implicit one:

```
@DIRTY_CANVAS 1 ;  @DIRTY_TOOLS 2 ;  @DIRTY_WIDTHS 4 ;
@DIRTY_PATS   8 ;  @DIRTY_CHROME 16 ;  @DIRTY_ALL 31 ;
@damage! ( bits -- )    ( OR into the damage word )
```

`draw-all` becomes `draw-damaged`: it repaints only the flagged regions and never wipes the whole
window (`system_begin_frame` does not clear — `src/system.c:629` — so untouched pixels persist
correctly). `APP::on-resume!` and the SF/confirm overlays set `DIRTY_ALL`. A pencil stroke sets
`DIRTY_CANVAS` plus a canvas sub-rect, so it repaints ~20 pixels rather than 440×280.

The rule that fixes the reported bug: **any region that is cleared must be in the same damage bit as
the thing drawn on top of it.** The pattern strip clears and redraws together, or not at all.

---

## Step 4 — tools, line widths, patterns

`@tool` gains the 20 MacPaint tools; `@pen-width` (0=none,1,2,3,4,8) and `@pat-id` (0..37) join it in
small state. Drawing gains:

- **Pattern-aware everything.** Pencil, line, and shape outlines currently hardcode black
  (`plot-black`, `frame-rect-pix` at `apps/Easel.lux:337` stores literal `1`). In MacPaint the pattern
  applies to fills; the *pen* is black. Keep pencil black (authentic), but route rect/oval/roundrect/
  freeform/polygon fills through `BITMAP::hspan-pat` with the current pattern, and stroke their
  outlines at `pen-width`.
- **Pen width** on line, shapes, and brush strokes: a `stroke-to` that stamps a `w×w` nib rather than
  a single pixel, with `pen-width 0` meaning "no outline" for the shape tools.
- **Brush shapes**: 32 shapes in a 8×4 grid, MacPaint's set (dots, squares, horizontal/vertical/
  diagonal bars at several sizes). Stored as 16×16 masks in `BRUSHES`. `Goodies > Brush Shape` and
  double-clicking the brush tool both open a modal picker built from `UI::overlay-*`
  (`lib/ui.lux:3090-3196`), which is what the Esc menu and the picker's About box already use.
- **38 patterns**, up from 16, as 8×8 bytes in `PATTERNS`. `Goodies > Edit Pattern` and
  double-clicking a swatch open a FatBits-style 8×8 editor over the strip.
- **Roundrect, freeform, polygon** tools: roundrect via `DRAW::paint-rrect`'s span algorithm ported to
  the bitmap; freeform = record the drag path, close it, scanline-fill; polygon = click-to-add-vertex,
  double-click or click-on-first-vertex to close.

**Double-click shortcuts** (authentic MacPaint), via `TIME::milli@` (`lib/time.lux:33`) with a ~400 ms
window and a "same target" check, mirroring the list-widget approach at `lib/ui.lux:1699`:
pencil → toggle FatBits · eraser → erase page · lasso/marquee → select all ·
brush → Brush Shape · hand → Show Page · pattern swatch → Edit Pattern.

---

## Step 5 — selection: marquee and lasso

The largest functional gap. State: `sel-active`, `sel-floating`, `sel-x/y/w/h`, plus `SEL_MASK` and
`SEL_BITS`.

- **Marquee** — drag a rect; `SEL_MASK` = that rect. Shift constrains to a square.
- **Lasso** — record the freehand path, close it, scanline-fill into `SEL_MASK`, then **shrink to the
  tight bounding box of black pixels inside the region**, which is the behaviour that makes MacPaint's
  lasso feel like a lasso.
- **Marching ants** — dashed outline whose phase advances off `TIME::milli@`. It is the one thing that
  must repaint while otherwise idle; it sets only its own damage rect, so it costs a handful of
  fill-rects per frame, not a full canvas scan.
- **Drag to move** — on mouse-down inside an active selection: lift the masked pixels into `SEL_BITS`,
  white the source region (unless copying), then blit `SEL_BITS` at the drag offset each frame.
  Dropped on mouse-up; committed on tool change, Enter, or a click outside.
- **Option-drag to copy** — mouse packets carry no modifiers (`src/system.c:669`), so latch Alt from
  the keyboard stream into `alt-mod` exactly as `shift-mod` is latched today (`apps/Easel.lux:1449`).
- **Cut / Copy / Paste / Clear** through `/sys/snarf`, following Nib's pattern
  (`apps/Nib.lux:1293-1305`). Format: `EASS`, `w` u16 LE, `h` u16 LE, then packed 1bpp rows, then the
  mask. A full-page selection is 51,840 + 51,840 + 8 = 103,688 bytes, which **exceeds the 64 KB
  snarf buffer** — so cut/copy clips to the selection's tight bounds first, and refuses (with the
  page left untouched) if the result still will not fit. Paste centres the floating selection in the
  viewport.

---

## Step 6 — Edit-menu transforms

All operate on the selection when one is active, otherwise the whole page; all `snapshot` first.

| Item | Implementation |
|---|---|
| Undo | `BITMAP::copy` swap through `SCRATCH`, as today but 1bpp |
| Cut / Copy / Paste / Clear | Step 5 |
| Invert | `BITMAP::invert` — word-wise XOR |
| Fill | `BITMAP::hspan-pat` over the masked region with the current pattern |
| Trace Edges | for each pixel, OR of the 4-neighbour differences; two-pass through `SCRATCH` |
| Flip Horizontal | per-row bit reversal, table-driven on bytes |
| Flip Vertical | row swap through `SCRATCH` |
| Rotate | 90° CW; non-square selections change `sel-w`/`sel-h`, page rotation is centre-cropped |

---

## Step 7 — text tool, Font / Size / Style

`/dev/draw`'s DrawChar and DrawCFFGlyph paint the framebuffer, not a guest bitmap, so text must be
rasterized into the page in Lux. `lib/cff.lux` already does exactly the needed decoding and is
already used this way by Illumos (`apps/Illumos.lux:130`):

```
CFF::use ( addr t -- )      CFF::glyph ( ch -- addr )
CFF::width@ ( ch -- w )     CFF::pixel@ ( ch x y -- bit )
```

Load `/sys/font/chicago`, `/sys/font/geneva`, `/sys/font/monaco` into `MM_APP_BULK_BUFFER` at startup
(`CFF::LEN12`-sized), then `@draw-glyph-into-page { base x y ch scale style -- adv }` stamps
`CFF::pixel@` through `BITMAP::set`.

- Click with the text tool places an insertion caret; typing appends; Backspace deletes; Return starts
  a new line at the original x. Text stays *floating* (redrawn each frame, undoable as one unit) until
  you click elsewhere or change tools, then it is committed to the bitmap — MacPaint's behaviour.
- **Font** menu: Chicago / Geneva / Monaco as `UI::radio-item`s.
- **Size** menu: 12 / 24 / 36 as integer scale 1 / 2 / 3.
- **Style** menu: `UI::check-item`s — Plain, Bold (OR with a 1px x-shift), Italic (per-row x shear),
  Underline (a span under the baseline), Outline (dilate minus original), Shadow (offset copy OR'd
  underneath). All synthesized from the one bitmap face, since these are fixed CFF blobs.

---

## Step 8 — viewport, hand, Show Page, FatBits

- `view-x` / `view-y` are the viewport origin on the 576×720 page, clamped to
  `[0, 576-440] × [0, 720-280]`.
- **Hand tool** drags the viewport (page moves with the cursor).
- **Goodies > Show Page**: modal overlay showing the whole page reduced 1:3 (192×240), with a draggable
  rectangle marking the viewport, and OK / Cancel. Reduction samples through `SCRATCH`, OR-ing each
  3×3 block so ink survives.
- **FatBits**: 8× zoom of a 55×35 window, with a 1:1 inset in the top-left corner (MacPaint shows one)
  and arrow-key panning. In FatBits the pencil toggles the pixel under the cursor.
- **Grid**: 8-px dot grid, and snapping for the shape tools when on.

---

## Step 9 — file format and menus

- **`EAS2`**: magic `EAS2`, `w` u16 LE, `h` u16 LE, then `PAGE_BYTES` of packed 1bpp — byte-identical
  to the in-memory `CANVAS`, so save is one `VFS::write` and load is one `VFS::read`.
- **`EAS1` compatibility**: the existing 512×342 reader is kept; an EAS1 file loads centred on the
  576×720 page. Saving always writes EAS2.
- **File**: New, Open, Save, Save As, Revert, Quit. `menu-quit` is a bare `HALT` today
  (`apps/Easel.lux:963`) — route it, and `menu-open`, through the existing dirty-confirm sheet
  (`confirm-new-*`, `apps/Easel.lux:877-931`) that only New uses now.
- **Goodies**: Grid, FatBits, Show Page, Edit Pattern, Brush Shape, Brush Mirrors.
- Silent I/O failure (`save-file` / `load-path` just `drop`, `apps/Easel.lux:807,819`) becomes a
  one-button `UI::overlay-*` alert.

---

## Files

| File | Change |
|---|---|
| `lib/bitmap.lux` | new — 1bpp page primitives (Step 1) |
| `lib/draw.lux` | add `batch-begin` / `batch-flush`; route existing primitives through the buffer |
| `apps/Easel.lux` | the rewrite — every step above |
| `include/memory_map.h` | document Easel's buffer layout in the bulk band, as the comment style there asks |
| `docs/ui.md:102` | rewrite the Easel paragraph; add `lib/bitmap.lux` to the layer table at `:108` |
| `docs/user-manual.md:754-830` | rewrite §9 for 20 tools, selection, text, Show Page, EAS2 |
| `docs/memory-map.md` | note the new bulk-band tenants |

No C source changes. Nothing in `src/` is touched, and no `/dev/draw` command or opcode is added —
`AGENTS.md`'s "adding a command is allowed; adding an opcode is not" line stays untested here.

---

## Verification

**Build**

```bash
make && make apps          # luxc must compile Easel.lux and the two libs clean
./bin/test_vm && ./bin/test_vfs && ./bin/test_compiler
```

The last three must still pass — `lib/draw.lux` is on the path of every app, so a batching regression
would show up there first.

**Bitmap unit test.** `lib/bitmap.lux` is pure logic and should not need the GUI to test. Add
`tests/bitmap.lux`, compiled headless and run under `bin/nux`, printing pass/fail through
`CMD_DEBUG_PRINT` (20). Cover: `set`/`get` round-trip at every bit position in a byte; `hspan` with
both edges partial; `blit` with overlapping source and destination; `flip-h` twice = identity;
`rotate` four times = identity; `copy` then `invert` then `invert` = original.

**Performance — this is the check that the reported symptoms are actually gone.**

```bash
NUXVM_FRAME_DEBUG=1 ./bin/cloister apps/Easel.bin
```

Logs per-phase frame time once a second. The target is that a full `draw-damaged` completes inside one
host tick with the page fully black — i.e. the worst case. Before the change, `draw-canvas-1x` alone
exceeds the 1,000,000-cycle budget (`src/machine.c:46`) on a *blank* page, so any measurement that
shows a completed frame is already an improvement; the bar is that painting the whole page black does
not make it worse.

```bash
NUXVM_MENU_DEBUG=1 ./bin/cloister apps/Easel.bin
```

Logs mouse motion and every `MB_HOVER` transition with timestamps (`src/cloister.c:443`). Sweeping the
cursor across an open File menu should show hover changes tracking the motion events within a frame or
two, not lagging tens of them.

**Interactive, against the reported failures.** Launch `./bin/cloister apps/Easel.bin`, then:

1. All 38 swatches stay visible while drawing, while opening and closing menus, and after Undo,
   FatBits, and dismissing the file dialog. *(This is bug 1; it is the fastest thing to confirm.)*
2. Click each of the 20 tools; each inverts in the palette and its drag does the right thing on the
   page immediately, not several frames later.
3. Click every menu title; the drop-down opens on the click, tracks hover, and applies on release.
4. Marquee a region, drag it, Option-drag to copy it, Cut, Paste, Clear.
5. Lasso around a shape and confirm the selection hugs the ink rather than the drag rectangle.
6. Each Edit transform, once with a selection and once without.
7. Text: click, type across a line wrap, switch font/size/style mid-entry, commit by clicking away,
   then Undo — the whole text block should disappear as one unit.
8. Hand-scroll to each corner of the 576×720 page; Show Page; FatBits + arrow panning.
9. Save as EAS2, New, re-open. Then open an EAS1 file written by the current build and confirm it
   lands centred.
10. Esc raises Continue / Restart / Quit, and Continue repaints the whole window intact — the
    `APP::on-resume!` path is the one that exposed bug 1 in the first place.
