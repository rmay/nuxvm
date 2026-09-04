# Illumos: restore the 256-glyph collection grid

## Context

Illumos (`apps/Illumos.lux`) is the CFF bitmap font editor. Its right-hand panel
is documented in `docs/user-manual.md:373` as "**Collection** (right): all 256
glyphs in a 16-wide grid", but that panel never paints.

Cause — `apps/Illumos.lux:199-206`:

```
@draw-all ( -- )
    APP::draw-fd 0 0 APP::width APP::height CLR_BG DRAW::fill-rect
    draw-editor
    draw-hint
    ( draw-collection )        <-- `( ... )` is a Lux comment
    draw-pangram
    UI::draw
;
```

`draw-collection` is commented out. It was disabled in commit `508a74f`, the same
commit that added the incremental `col-draw-cell` / `col-full` / `last-sel`
machinery — it reads as a perf debugging edit that was never reverted. The
hit-testing is still live (`@in-collection?` :224, `@select-at` :230), so clicking
the blank region silently moves the edit cursor.

Restoring the call alone is **not** sufficient. `draw-all` clears the whole
framebuffer every dirty frame (:200), but `draw-collection` (:163-178) only
repaints all 256 cells when `col-full` is 1 — and `col-full` is set only in
`reset-doc`, `load-bytes` and `start`, then cleared after the first pass (:172).
The grid would flash once and then vanish down to two cells.

Intended outcome: the grid is always visible and correct, without reintroducing
the per-mouse-move repaint cost that motivated disabling it.

## Approach

### 1. Make the grid correct (`apps/Illumos.lux`)

`draw-all` is a *full* repaint — it clears the window — so the grid it draws must
be full too.

- In `@draw-all` (:199), replace `( draw-collection )` with `1 col-full STOREI`
  followed by `draw-collection`.
- Update the stale comment at :144-147, which currently claims `draw-collection`
  "redraws just this instead of all 256 cells whenever it can" — after this change
  the incremental branch only fires from a non-clearing caller (step 2).

### 2. Stop menus forcing a full repaint

Illumos's `@on-mouse` (:355-360) does the naive thing:

```
UI::menu-open?
OVER UI::feed
UI::menu-open? OR [ drop APP::dirty! ] ...
```

so *every* mouse event while a menu is open triggers `draw-all` — now including
256 glyph cells. This is exactly the cost the original author was avoiding.

Adopt the pattern already proven in Easel (`apps/Easel.lux:4313-4344`, with its
long comment explaining the damage reasoning). Use `UI::menu-active`
(`lib/ui.lux:2069`) instead of `UI::menu-open?` and only dirty when a dropdown
that *was* drawn has moved or closed:

```
UI::menu-active GIRD was
    OVER UI::feed
    was -1 > was UI::menu-active = 0 = AND [ drop APP::dirty! ] [ ...normal... ] ?:
UNGIRD
```

Opening from nothing (`was = -1`) paints on top of correct pixels and uncovers
nothing; hovering a different item inside an already-open menu leaves the
dropdown rectangle in place. Both are handled by the `UI::draw` branch already in
`@on-frame` (:500-505). Keep the existing `MOUSE_UP` reset of `drag-w`/`paint-m`
ahead of this block.

### 3. Measure before adding an incremental path

The remaining hot path is a paint drag: `paint-at` → `mark-dirty` → `draw-all`
on every `MOUSE_MOVE`, which now includes the 256-cell pass.

Per `docs/` and prior root-causing on Easel, measure with a headless
`machine_tick` probe rather than guessing (`src/machine.c`, and the harness style
in `src/test_vm.c`): count VM cycles for one `draw-all` with and without
`draw-collection`. Only if a paint-drag frame lands over budget, split the
repaint:

- keep `draw-all` as the full clear-and-repaint (resume, load, new, menu damage);
- add a `@draw-edit` that fills only the editor / hint / pangram regions and lets
  `draw-collection` take its existing incremental `last-sel` + `sel` branch;
- route `mark-dirty` (paint, width drag, glyph shift) and selection moves through
  it via a `need-full` flag read in `@on-frame`.

Do not do this preemptively — the `col-full` / `last-sel` machinery is already in
place for it, so it is a small follow-on if the numbers ask for it.

### 0. Mirror this plan into the repo

Plan mode allows editing only the plan file, so as the first implementation step
copy this plan to `docs/illumos_collection_grid.md`.

## Files

- `apps/Illumos.lux` — the only source change (steps 1 and 2).
- Reference only: `apps/Easel.lux:4313-4344` (menu damage pattern),
  `lib/ui.lux:2062-2069` (`menu-open?` / `menu-active`).

## Verification

1. `make apps` (or `make`) — regenerates `apps/Illumos.bin` via
   `bin/luxc -target graphical`. Note `apps/Illumos.bin` is currently newer than
   the `.lux`, so it must be recompiled or the fix will not appear.
2. `./bin/cloister apps/Illumos.bin` and confirm:
   - the 16x16 grid renders at x=440, y=36, all 256 glyphs of Chicago;
   - clicking a cell selects it and both the old and new cell redraw correctly;
   - arrow keys move the selection and the grid highlight follows;
   - painting pixels in the editor updates the corresponding grid cell;
   - `File > New 8x8` / `16x16` / `24x24` and `File > Open` of
     `resources/geneva12.cff` and `resources/monaco12.cff` each repaint the full
     grid at the new tile size.
3. Menu check: open File, slide across to Edit/Glyph/Move, hover items, close —
   no stale dropdown pixels over the grid, and no visible lag tracking the
   pointer.
4. `make test` for the existing suites (nothing here touches C, but the app ROMs
   are exercised by the sandboxed app tests added in `286b98c`).
