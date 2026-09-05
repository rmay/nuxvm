# Lux UI Toolkit

Reusable System 6–looking controls for Cloister apps. Architecture follows Plan 9 / Inferno (`libcontrol`): named controls, stack words, no widget inheritance, no layout manager.

## Components

Every control is added the same way: **text, x, y, width, height, handler**.
The handler is a quotation. It runs with `( val -- )` after the mouse is done —
never from inside hit-test.

```lux
INCLUDE "lib/app.lux"
INCLUDE "lib/ui.lux"

@on-ok     ( val -- ) drop save-file ;
@on-cancel ( val -- ) drop ;
@on-hidden ( val -- ) ... ;          ( 0 or 1 )
@on-vol    ( val -- ) ... ;          ( new integer )
@on-left   ( val -- ) drop ... ;

@start
    T"My App" APP::init
    UI::new
    T"Show hidden files" 56 88 200 12 [ on-hidden ] UI::checkbox
    T"Left"   56 140 80 12 grp [ on-left ] UI::radio
    T"Volume" 320 80 17 112 0 7 4 [ on-vol ] UI::slider
    T"V" 412 80 16 112 0 20 6 [ on-vbar ] UI::vscroll
    T"H" 360 200 120 16 0 20 8 [ on-hbar ] UI::hscroll
    T"Cancel" 248 228 59 20 [ on-cancel ] UI::button
    T"OK"     319 228 59 20 [ on-ok ]     UI::button
    T"OK" UI::default
    APP::width UI::menubar
    T"File" UI::menu
    T"Save" [ on-ok ] UI::item
    T"View" UI::menu
    T"Hex"  [ on-hidden ] UI::check-item
    [ UI::feed ] APP::on-mouse!
    [ on-frame ] APP::on-frame!
    APP::loop
;

@on-frame ( -- )
    UI::handle
    ( ... app chrome ... )
    UI::draw
;
```

| Word | Stack | `val` in the handler |
|---|---|---|
| `UI::new` | `--` | reset the toolkit |
| `UI::button` | `text x y w h handler --` | `1` on release inside |
| `UI::checkbox` | `text x y w h handler --` | `0` or `1` |
| `UI::radio` | `text x y w h group handler --` | `1` when newly selected |
| `UI::slider` | `text x y w h min max val handler --` | new integer |
| `UI::vscroll` | `text x y w h min max val handler --` | new integer |
| `UI::hscroll` | `text x y w h min max val handler --` | new integer |
| `UI::default` | `text --` | mark that button as the Return target |
| `UI::enter` | `-- posted?` | post the default button |
| `UI::menubar` | `w --` | one bar at `(0,0,w,20)` |
| `UI::menu` | `title --` | add a pull-down to the bar |
| `UI::item` | `text handler --` | command item (`EV_PRESS 1`) |
| `UI::check-item` | `text handler --` | check item (`EV_CHECK` / `EV_UNCHECK`) |
| `UI::radio-item` | `text group handler --` | radio item (`EV_SELECT 1`) |
| `UI::sep` | `--` | dashed separator, not clickable |
| `UI::item-set` | `name 0\|1 --` | seed or sync a check / radio mark |
| `UI::item@` | `name -- 0\|1` | read a check / radio mark |
| `UI::menu-open?` | `-- f` | a pull-down is open |
| `UI::escape` | `-- closed?` | close the pull-down if open |
| `UI::export-fd!` | `fd --` | bind `/dev/menu`; `0` keeps a local bar |
| `UI::export` | `--` | write a binary menu snapshot to that fd |
| `UI::menu-pick` | `name --` | apply an item by label |
| `UI::exported?` | `-- f` | true when `/dev/menu` is bound |
| `UI::local-bar!` | `f --` | 1 = also paint/feed File/Edit in the window |
| `UI::feed` | `mpkt --` | give `/dev/mouse` to the toolkit |
| `UI::handle` | `--` | drain the ring and CALL handlers |
| `UI::draw` | `--` | paint every component |
| `UI::label` | `text x y --` | Chicago static text |
| `UI::tooltip-draw` | `fd text x y max-x --` | small bordered hover-help box anchored at `(x,y)`, clamped to `max-x` on the right. There's no built-in per-widget hover state; callers track their own hover-idle timer (`TIME::milli@`, same idiom as double-click detection) and call this once the delay elapses. See `apps/Easel.lux`'s tool-palette tooltips (`hover-tool`/`hover-t0`) for a worked example. |
| `UI::groupbox` | `text x y w h --` | labeled FrameRect; title sits on the top edge |
| `UI::list` | `text x y w h handler --` | group box with a selectable item column. `text` is the header and the identity. Click posts `EV_PRESS` with the row index (`val` in the handler). |
| `UI::list-add` | `name item --` | append a NUL-terminated label |
| `UI::list-clear` | `name --` | drop every item |
| `UI::list-count` | `name -- n` | |
| `UI::list-sel@` / `UI::list-sel!` | `name -- idx` / `name idx --` | `-1` means none |
| `UI::list-item@` | `name idx -- ptr` | item string, or `0` |
| `UI::list-active` | `name --` | focus that list: inverted title, inverted selection. Sibling lists go inactive (plain title, outline selection). |

`text` is the label and the identity (`T"OK"`). Radio `group` is one stored string pointer (`T"align" grp STOREI` then `grp LOADI` — `T"` is not interned). Checkbox/radio `w h` is the clickable strip; the mark is the 12px System 6 box at `x,y`.

The menu bar is always the top 20px strip. Click a title to open, click an item to post and close, click outside or Esc to dismiss. An open menu sets `APP::modal!` so Esc does not raise the system menu. Check and radio marks live on the item; a menu radio group is **not** linked to a panel `UI::radio` group even if the group string matches. `draw` paints the open dropdown last, on top of other controls.

Wire keyboard Return to `UI::enter` and Esc to `UI::escape`. The ring is still there (`UI::poll`) if an app wants raw records instead of handlers.

## Document session (`lib/doc.lux`)

File/Edit, unsaved-changes, and the Standard File picker are the same in every document ROM. `DOC` owns that session; the app still serializes its own bytes.

```lux
INCLUDE "lib/doc.lux"

@load-bytes ( path -- ) ... ;
@save-bytes ( path -- ) ... ;
@reset-doc  ( -- )      ... ;

@start
    T"My App" APP::init
    UI::new
    APP::width UI::menubar
    T"My App" T"untitled.myapp" DOC::init
    [ load-bytes ] DOC::on-load!
    [ save-bytes ] DOC::on-save!
    [ reset-doc  ] DOC::on-new!
    DOC::file-menu
    DOC::edit-menu          ( omit if there is no Cut/Copy/Paste )
    [ on-mouse ] APP::on-mouse!
    [ on-frame ] APP::on-frame!
    APP::loop
;
```

| Word | Stack | Meaning |
|---|---|---|
| `DOC::init` | `title default-path --` | path, untitled name, window title, SF wiring |
| `DOC::path` | `-- ptr` | current document path (no leading `/`) |
| `DOC::dirty?` / `DOC::dirty!` / `DOC::clean!` | `-- f` / `--` | unsaved-changes; `dirty!` also `APP::dirty!` and appends ` *` to the title |
| `DOC::on-load!` / `DOC::on-save!` | `xt --` | quotations `( path -- )` |
| `DOC::on-new!` / `DOC::on-quit!` | `xt --` | quotations `( -- )`; quit defaults to `HALT` |
| `DOC::file-menu` | `--` | File > New / Open / Save / Save As / Quit. Dirty New, Open, and Quit raise `DIALOG::confirm-save` |
| `DOC::edit-menu` | `--` | Edit > Cut / Copy / Paste / Select All (no-ops until `DOC::on-cut!` and friends) |
| `DOC::menu-save` / `DOC::menu-new` / … | `--` | the same actions as the menu items, for Cmd-S and tests |
| `DOC::confirm-xt` | `xt --` | if dirty, confirm-save then CALL xt; if clean, CALL xt now. For Revert and New 8x8 / 16x16 / … |
| `DOC::pick` | `path --` | SF Open vs Save As (leading `/` stripped) |

Quill, Tabula, Nib, Illumos, Easel, and Whittle all use this session. Apps whose File menu is not the five standard items (Illumos/Whittle New sizes, Easel Revert) still call `DOC::menu-*` / `DOC::confirm-xt` and keep their own item list. Hello, Calculator, and games do not INCLUDE `lib/doc.lux`. Formats stay per-app (`.quill`, `.tabula`, `.eas`, `.nib`, `.cff`, `.csf`).

Cloister is a fantasy machine: one program at a time owns the 960×720 screen and its own menu bar. `./bin/cloister` boots `apps/Picker.lux`, a Lux guest that shows two group-box lists of sources (Lux from `apps/` on the left, Fluxio from `apps/fluxio/` on the right), each compiled in-process when picked. **Cloister > About Cloister** is a modal info box (`APP::modal!`, dithered desk, title bar, close box, OK). Click or Enter writes the path to `/sys/launch` and HALTs; the host loads that ROM. `./bin/cloister apps/Quill.bin` boots that ROM directly. Esc in an app raises Continue / Restart / Quit. Halt (Quit) returns to the picker, or exits if the ROM was passed on the command line or Quit was chosen in the picker itself.

A group box is FrameRect with the title punched through the top edge. `UI::groupbox` is that chrome alone (place radios or other controls inside by coordinates). `UI::list` is the same chrome plus a clickable column of strings — two of them side by side is how the picker does headers-and-columns, with no layout manager.

**Illumos** (`apps/Illumos.lux`) is the CFF font editor: 16×16 glyph collection, magnified pixel editor, width rule, pangram. Open/Save through `SF::`. See `docs/CFF.md`. User-facing: [user-manual.md](user-manual.md).

**Nib** (`apps/Nib.lux`) is MacDraw-simple object drawing: arrow / line / rect / roundrect / oval / text, pen 1–3 pt, fill none/white/black, grid and snap. File > New/Open/Save/Save As | Quit, Edit > Cut/Copy/Paste/Duplicate/Clear, Arrange > Bring to Front / Send to Back. Save files are `NIB 1` text. `./bin/cloister apps/Nib.bin`. User-facing: [user-manual.md](user-manual.md).

**Easel** (`apps/Easel.lux`) is a MacPaint clone: 20 tools (lasso, marquee, hand, text, fill, spray, brush, pencil, line, eraser, rect/filled, roundrect/filled, oval/filled, freeform/filled, polygon/filled) over a 4-bit colour (16 palette indices) 576×720 page, viewed through a 480×416 scrollable viewport. Selection (marquee + lasso) supports move, Option-drag copy, and Cut/Copy/Paste/Clear via `/sys/snarf`. Edit also has Invert/Fill/Trace Edges/Flip H/Flip V/Rotate 90. Text tool rasterizes Chicago/Geneva/Monaco via `lib/cff.lux` at size 12/24/36 with Bold/Italic/Underline/Outline/Shadow, all combinable. 38 patterns, 32 preset brush shapes, a 0/1/2/3/4/8px pen-width strip, Goodies > Grid / FatBits / Edit Pattern / Brush Shape / Show Page / Mirror Horizontal / Mirror Vertical (brush dabs also stamp reflected about the page center; both on together give 4-way symmetry). The page is packed 4bpp via `lib/cmap.lux`, one nibble per pixel indexing the 16-colour system palette (selection masks stay 1bpp in `lib/bitmap.lux`). `/dev/draw` writes are batched via `lib/draw.lux` so a full repaint stays inside one host frame. Save files are `EAS4` (whole packed 4bpp page). The older 2bpp `EAS3` still opens and widens index-for-index — palette entries 0-3 are exactly the four grays it used — but is never written back. A 4×4 colour-ink picker sits in the pattern strip’s left box. `./bin/cloister apps/Easel.bin`. User-facing: [user-manual.md](user-manual.md). Design/status: `easel_plan.md`.

**Tabula** (`apps/Tabula.lux`) is the spreadsheet: columns A–Z, sparse rows 1…99999, strings/ints/floats and basic formulas. File > New/Open/Save/Save As | Quit, Edit > Cut/Copy/Paste/Select All, Formula > Calculate (Esc stops a running pass). Entry bar shows formula source; the grid shows the last computed value (or `#DIV/0!` / `#VALUE!` / `#REF!` / `#CIRC` / `#NAME?` / `#STOP`). Save files are `TABULA 400` sparse CSV (`addr,source`); formula source is stored, not the cache. Value fields use backslash escapes (`\\`, `\t`, `\n`, `\r`, `\,`, and `\=` for a leading equals that is not a formula). Integer arithmetic only: `+ - * /`, parentheses, unary minus, `A1` refs, `SUM(A1:B10)`. `./bin/cloister apps/Tabula.bin`. User-facing: [user-manual.md](user-manual.md).

## Layers

| File | Role |
|---|---|
| `lib/geom.lux` | Point (`x y`) and Rect (`minx miny maxx maxy`, max exclusive). `GEOM::clamp` (`n lo hi -- n`). |
| `lib/str.lux` | `printable?`, `int-to-str`, `strcpy`/`strlen`/`streq`, `last-slash`, `basename` |
| `lib/vfs.lux` | Plan 9 file ops plus `VFS::sys-file` (`rel dest -- dest`) to join `/sys/file/`, `VFS::snarf-read`/`snarf-write`, `VFS::load-i32`/`save-i32` |
| `lib/bitmap.lux` | Packed 1bpp page primitives (`addr`/`get`/`set`, `word@`/`word!`, `hspan`/`hspan-pat`, `clear`/`fill`/`copy`/`invert`/`blit`). Easel uses this for the lasso selection mask, not the document. |
| `lib/graymap.lux` | Packed 2bpp grayscale page (four levels, 0 white .. 3 black), same 576×720 geometry as `BITMAP`. No longer a document format — kept for reading Easel’s legacy `EAS3` files. |
| `lib/cmap.lux` | Packed 4bpp colour page (16 palette indices, one nibble per pixel), same 576×720 geometry. Easel’s canvas/undo/tmp. `CMAP::widen-2bpp` converts a `GRAYMAP` page in place of a copy; `CMAP::invert` is one `0x33333333` XOR per word. See `docs/palette.md`. |
| `lib/mem.lux` | Bump heap at `0xA00000` |
| `lib/draw.lux` | QuickDraw: `/dev/draw` packing plus `paint-rect` / `frame-rect`, `paint-oval` / `frame-oval`, `paint-rrect` / `frame-rrect`, `paint-tri`, `hline` / `vline` / `line` / `dash-h`, `x-mark` / `check-mark`, `fill-r` / `stroke-r`, `char-w` / `char-h`. `DRAW::set-font` 0 Chicago / 2 Geneva / 3 Monaco; UI chrome stays Chicago. `DRAW::chan!` / `DRAW::grayscale` / `DRAW::gray` select the draw channel (RGB default, k8/k2/k1 grayscale, or c4 — snap to the 16-colour system palette). `DRAW::PAL_*` are the sixteen palette inks and `DRAW::clut` maps a 4bpp index to one; `DRAW::rgb` packs r/g/b. See `docs/palette.md`. |
| `lib/ui.lux` | Controls. Faces call DRAW primitives (button → RoundRect, radio → Oval, checkbox → Rect + `x-mark`, scrollbar arrows → `paint-tri`, menu check → `check-mark`, separator → `dash-h`, group box / list → FrameRect + title) |
| `lib/sf.lux` | System 6 Standard File picker (`SF::`) |
| `lib/dialog.lux` | In-app Save/Don't Save/Cancel and one-button alert (`DIALOG::confirm-save`, `DIALOG::alert`). Overlay buttons; `APP::modal!`. |
| `lib/app.lux` | Devices, loop, `dirty!`. Menus/buttons are Chicago. `APP::font-menu` (Quill, Tabula) picks the document face (`APP::use-text-font`, `APP::text-font`). Overlay buttons (`UI::overlay-*`) are the Esc system menu and the picker’s About box. `APP::grayscale!` after `APP::init` puts `/dev/draw` in 8-bit gray; `APP::palette!` puts it in the 16-colour system palette (`docs/palette.md`); `APP::color!` restores unrestricted RGB. `APP::beep` writes a 4-byte packet to `/dev/audio` (opened in `init`; no-op if missing). |
| `lib/menu.lux` | Leftover quotation menu bar (Quill uses `UI::`) |

## Standard File (`SF`)

Modal System 6-style open / save-as dialog. One instance per app. Centered on the window.

```lux
INCLUDE "lib/sf.lux"
IMPORT SF

@on-pick ( path -- ) ... ;
@on-open ( val -- ) drop SF::show ;
@on-save-as ( val -- ) drop PATH SF::show-save ;

[ on-pick ] SF::on-ok!
( in the mouse / kbd / frame vectors: )
SF::open? [ SF::mouse ] [ UI::feed ] ?:
SF::open? [ SF::kbd ] [ ... ] ?:
SF::open? [ SF::draw ] ?
```

| Word | Stack | Notes |
|---|---|---|
| `SF::show` | `--` | GetFile: open at `/`, **Open** button, no name field |
| `SF::show-save` | `name --` | PutFile: basename in the name field, **Save** button; starts in `name`'s directory if it has one |
| `SF::hide` | `--` | dismiss, no handler |
| `SF::open?` | `-- f` | dialog is up |
| `SF::draw` | `--` | paint if you already checked `open?` |
| `SF::mouse` | `mpkt --` | take `/dev/mouse` while open |
| `SF::kbd` | `kpkt --` | Return confirms, Esc cancels; in save mode, printable keys edit the name |
| `SF::on-ok!` | `quot --` | `( path -- )` after a file is chosen / saved |
| `SF::on-cancel!` | `quot --` | `( -- )` after Cancel / Esc |
| `SF::hidden!` | `f --` | 1 lists names that start with `.` |
| `SF::path` | `-- ptr` | last chosen path |

`show` / `show-save` set `APP::modal!` so Esc goes to the picker, not the system menu. The path popup above the list is the folders you have entered. The name field is fully selected on open, so the next printable character replaces it. Empty names do not Save. `/` is rejected while typing. Existing files are overwritten silently.

A radio handler fires only when a new item turns on. Re-clicking the selected radio does nothing. Siblings in the same group clear; a group never ends up empty.

The slider is vertical (System 6 Speaker Volume): max at the top. `val` is posted only when the integer changes.

Scrollbars are System 6 16px bars: arrow buttons at both ends, dithered track, white thumb. Min is at the top (`vscroll`) or left (`hscroll`). Arrow click steps by 1; track click pages; the thumb drags. `val` is posted only when the integer changes.

A button is one generic System 6 push button: 20px high, r=3 rounded rect (DRAW::paint-rrect / frame-rrect with ovalWidth 6), Chicago label centered. `UI::default` adds the HIG ring. Press inverts; drag out un-inverts; the handler runs only on release still inside. Radios call DRAW::frame-oval / paint-oval. Checkboxes call a framed rect plus DRAW::x-mark. Scrollbar arrows call DRAW::paint-tri. A group box is FrameRect with the title punched through the top edge. A list is that group box plus inverted-row selection.

A menu bar is Chicago 12, 20px high, white with a 1px black rule. The open title and hovered item invert. Check and radio items show a checkmark in the left column, not the standalone checkbox X. Separators are a dashed line. Click-to-open, not press-and-hold.

## Controlset (legacy)

```lux
INCLUDE "lib/app.lux"
INCLUDE "lib/ui.lux"

T"demo" APP::init
UI::ctlnew

T"ok" UI::ctl-button  10 10 110 40 UI::rect!  T"OK" UI::text!  $on-ok UI::on-press!
T"a"  UI::ctl-checkbox 10 50 200 74 UI::rect!  T"Option A" UI::text!
T"b"  UI::radio    10 80 200 104 UI::rect! T"B" UI::text!  T"g" UI::group!

[ UI::mouse ] APP::on-mouse!
[ UI::kbd   ] APP::on-kbd!
[ UI::ctl-draw  ] APP::on-frame!
APP::loop
```

Rectangle arguments are Plan 9 min/max, not x/y/w/h. `10 10 110 40` is a 100×30 box.

### Constructors

`ctl-button` `ctl-checkbox` `ctl-radio` `ctl-slider` `scrollbar` `field` `ctl-label` `window` — each is `( name -- ctl )`.

Or `name kind create` with `K_BUTTON` … `K_WINDOW`.

### Stack words

| Word | Stack |
|---|---|
| `rect!` | `ctl minx miny maxx maxy --` |
| `text!` | `ctl ptr --` |
| `value!` / `value@` | `ctl n --` / `ctl -- n` |
| `range!` | `ctl lo hi val --` |
| `group!` | `ctl name --` (radio exclusivity) |
| `on-press!` `on-change!` | `ctl quot --` |
| `show!` `hide!` `focus!` `title!` `default!` | flags |
| `draw` | `--` |
| `mouse` | `mpkt --` |
| `kbd` | `kpkt --` |
| `poll` | `-- name ev \| 0` |

Old struct words (`button-init`, `button-draw`, …) remain for the leftover listbox and the legacy `ctl-*` controlset. `lib/menu.lux` remains for anything still on quotation menus. Calculator, UIDemo, and Quill use `UI::new`.

### Window chrome

`window` draws a System 6 title bar, close box, and border.

- `win-client ( ctl -- minx miny maxx maxy )`
- `win-hit ( ctl x y -- where )` — 0 miss, 1 client, 2 title, 3 close
- `win-chrome ( ctl -- )`

Apps can use these for in-window frames; Cloister itself does not manage overlapping process windows.

## Geometry (`GEOM`)

```
pt-in-rect?   ( x y minx miny maxx maxy -- bool )
xywh>rect     ( x y w h -- minx miny maxx maxy )
rect>xywh     ( minx miny maxx maxy -- x y w h )
rect-dx  rect-dy  rect-canon  rect-inset  rect-intersect
```

## Heap (`MEM`)

```
reset   ( -- )           rewind to 0xA00000
allot   ( n -- addr )    bump; 0 if the heap is exhausted
here    ( -- addr )
align   ( -- )
```

`UI::ctlnew` calls `MEM::reset`.

## Lux helpers used by the toolkit

Named locals and `FIELDS` are documented in `lux_tutorial.md`. Example:

```lux
FIELDS PT x y ;
@mid { a b -- m } a b + 2 / ;
```
