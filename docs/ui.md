# Lux UI Toolkit

Reusable System 6–looking controls for Cloister apps and the Shell. Architecture follows Plan 9 / Inferno (`libcontrol`): named controls, stack words, no widget inheritance, no layout manager.

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
| `UI::feed` | `mpkt --` | give `/dev/mouse` to the toolkit |
| `UI::handle` | `--` | drain the ring and CALL handlers |
| `UI::draw` | `--` | paint every component |

`text` is the label and the identity (`T"OK"`). Radio `group` is one stored string pointer (`T"align" grp STOREI` then `grp LOADI` — `T"` is not interned). Checkbox/radio `w h` is the clickable strip; the mark is the 12px System 6 box at `x,y`.

The menu bar is always the top 20px strip. Click a title to open, click an item to post and close, click outside or Esc to dismiss. An open menu sets `APP::modal!` so Esc does not raise the system menu. Check and radio marks live on the item; a menu radio group is **not** linked to a panel `UI::radio` group even if the group string matches. `draw` paints the open dropdown last, on top of other controls.

Wire keyboard Return to `UI::enter` and Esc to `UI::escape`. The ring is still there (`UI::poll`) if an app wants raw records instead of handlers.

Windows are chrome + hit-test. rio-style child VMs still own `/dev/draw`; the Shell offsets their commands into the client rect.

## Layers

| File | Role |
|---|---|
| `lib/geom.lux` | Point (`x y`) and Rect (`minx miny maxx maxy`, max exclusive) |
| `lib/mem.lux` | Bump heap at `0xA00000` |
| `lib/draw.lux` | QuickDraw: `/dev/draw` packing plus `paint-rect` / `frame-rect`, `paint-oval` / `frame-oval`, `paint-rrect` / `frame-rrect` (ovalWidth/Height), `hline` / `vline` / `line`, `x-mark`, `fill-r` / `stroke-r`, `char-w` / `char-h` |
| `lib/ui.lux` | Controls. Faces call DRAW primitives (button → RoundRect, radio → Oval, checkbox → Rect + `x-mark`) |
| `lib/sf.lux` | System 6 Standard File picker (`SF::`) |
| `lib/app.lux` | Devices, loop, `dirty!` |
| `lib/menu.lux` | Leftover quotation menu bar (Quill uses `UI::`) |

## Standard File (`SF`)

Modal System 6-style open dialog. One instance per app. Centered on the window.

```lux
INCLUDE "lib/sf.lux"
IMPORT SF

@on-pick ( path -- ) ... ;
@on-open ( val -- ) drop SF::show ;

[ on-pick ] SF::on-ok!
( in the mouse / kbd / frame vectors: )
SF::open? [ SF::mouse ] [ UI::feed ] ?:
SF::open? [ SF::kbd ] [ ... ] ?:
SF::open? [ SF::draw ] ?
```

| Word | Stack | Notes |
|---|---|---|
| `SF::show` | `--` | open at `/` |
| `SF::hide` | `--` | dismiss, no handler |
| `SF::open?` | `-- f` | dialog is up |
| `SF::draw` | `--` | paint if you already checked `open?` |
| `SF::mouse` | `mpkt --` | take `/dev/mouse` while open |
| `SF::kbd` | `kpkt --` | Return opens, Esc cancels |
| `SF::on-ok!` | `quot --` | `( path -- )` after a file is chosen |
| `SF::on-cancel!` | `quot --` | `( -- )` after Cancel / Esc |
| `SF::hidden!` | `f --` | 1 lists names that start with `.` |
| `SF::path` | `-- ptr` | last chosen path |

`show` sets `APP::modal!` so Esc goes to the picker, not the system menu. The path popup above the list is the folders you have entered.

A radio handler fires only when a new item turns on. Re-clicking the selected radio does nothing. Siblings in the same group clear; a group never ends up empty.

The slider is vertical (System 6 Speaker Volume): max at the top. `val` is posted only when the integer changes.

Scrollbars are System 6 16px bars: arrow buttons at both ends, dithered track, white thumb. Min is at the top (`vscroll`) or left (`hscroll`). Arrow click steps by 1; track click pages; the thumb drags. `val` is posted only when the integer changes.

A button is one generic System 6 push button: 20px high, r=3 rounded rect (DRAW::paint-rrect / frame-rrect with ovalWidth 6), Chicago label centered. `UI::default` adds the HIG ring. Press inverts; drag out un-inverts; the handler runs only on release still inside. Radios call DRAW::frame-oval / paint-oval. Checkboxes call a framed rect plus DRAW::x-mark.

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

`ctl-button` `ctl-checkbox` `ctl-radio` `ctl-slider` `scrollbar` `field` `label` `window` — each is `( name -- ctl )`.

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

The Shell uses these so a child VM's `/dev/draw` lands in the client, and only the title bar drags.

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
