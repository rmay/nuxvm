# Cloister User Manual

Quill, Illumos, Tabula, Nib, and Easel

This manual is for people who want to write, draw fonts, keep spreadsheets, and draw pictures on Cloister — the graphical host for NUX. It describes the five applications as they behave today: **Quill** (text), **Illumos** (bitmap fonts), **Tabula** (spreadsheets), **Nib** (object drawing), and **Easel** (pixel painting).

You do not need to know Lux, Fluxio, or the VM instruction set to use them.

---

## Contents

1. [What Cloister is](#1-what-cloister-is)
2. [Starting Cloister](#2-starting-cloister)
3. [The picker](#3-the-picker)
4. [Things every app shares](#4-things-every-app-shares)
5. [Quill — text editor](#5-quill--text-editor)
6. [Illumos — font editor](#6-illumos--font-editor)
7. [Tabula — spreadsheet](#7-tabula--spreadsheet)
8. [Nib — object drawing](#8-nib--object-drawing)
9. [Easel — pixel painting](#9-easel--pixel-painting)
10. [Moving work between apps](#10-moving-work-between-apps)
11. [File formats](#11-file-formats)
12. [Limits and known behavior](#12-limits-and-known-behavior)
13. [Keyboard reference](#13-keyboard-reference)

---

## 1. What Cloister is

Cloister is a small graphical machine: one 960×720 window, one program at a time. The running program owns the screen, the mouse, the keyboard, and a System 6–looking menu bar. There is no overlapping-windows desktop.

The five programs this manual covers:

| App | What it is | File |
| --- | --- | --- |
| **Quill** | Plan 9–style text editor, with a hex view | `.quill` (plain text) |
| **Illumos** | Bitmap font editor for Cloister Font Format | `.cff` |
| **Tabula** | 26-column spreadsheet with basic formulas | `.tabula` |
| **Nib** | MacDraw-style object drawing | `.nib` |
| **Easel** | MacPaint-style pixel painting | `.eas` |

All five live under `apps/` and run inside Cloister.

---

## 2. Starting Cloister

From the repository root:

```bash
make
./bin/cloister
```

You need a C compiler, pkg-config, and SDL2 (`brew install sdl2` on macOS, `apt install libsdl2-dev` on Debian/Ubuntu).

`./bin/cloister` with no arguments opens the **picker**. Passing a ROM boots that program directly:

```bash
./bin/cloister apps/Quill.bin
./bin/cloister apps/Illumos.bin
./bin/cloister apps/Tabula.bin
./bin/cloister apps/Nib.bin
./bin/cloister apps/Easel.bin
```

Quitting an app that was launched from the picker returns you to the picker. Quitting an app that was passed on the command line exits Cloister.

---

## 3. The picker

The picker is itself a Cloister program (`apps/Picker.lux`). The window title is **Cloister**.

```
┌─────────────────────────────────────────────────────────────┐
│ Cloister                                                    │  menu bar
├─────────────────────────────────────────────────────────────┤
│ Select a program    Enter or click to run                   │
│                                                             │
│  ┌─ Lux ──────────────────┐   ┌─ Fluxio ─────────────────┐  │
│  │ Calculator.lux         │   │ HelloCloister.bin        │  │
│  │ Illumos.lux            │   │ Quill.bin                │  │
│  │ Easel.lux              │   │                          │  │
│  │ Nib.lux                │   │                          │  │
│  │ Quill.lux              │   │                          │  │
│  │ Tabula.lux             │   │                          │  │
│  │ …                      │   │                          │  │
│  └────────────────────────┘   └──────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

- **Lux** (left) lists sources in `apps/`. Choosing one compiles and launches it.
- **Fluxio** (right) lists already-compiled bins in `apps/fluxio/`.
- Click a name, or highlight it and press **Enter**.
- **Left / Right** switch which list is active.
- **Up / Down** move the highlight.

**Cloister** menu

| Item | What it does |
| --- | --- |
| **About Cloister** | Modal info box (see below) |
| **Quit** | Leave Cloister |

### About Cloister

**Cloister > About Cloister** opens a centered System 6–style window over a dithered desktop. The lists and the rest of the picker do not receive clicks or keys until it closes.

The title bar reads **About Cloister** and has a close box on the left. The body says:

- Cloister
- A single-app fantasy machine
- NUX virtual machine
- Lux and Fluxio guest programs
- Plan 9 files, System 6 look
- Cloister is built on Nux VM.
- Authors: Russell May and AI Agents

**OK** is the default button.

| Action | Result |
| --- | --- |
| **OK**, or **Enter** | Close the box |
| Close box (title-bar square) | Close the box |
| **Esc** | Close the box |

While About is open, Esc does **not** raise the system overlay (Continue / Restart / Quit). That overlay still appears from Esc on the picker when About is closed.

This manual documents the Lux programs: `Quill.lux`, `Illumos.lux`, `Tabula.lux`, `Nib.lux`, and `Easel.lux`. A Fluxio port of Quill also appears in the right-hand column; it is not the complete editor described here.

---

## 4. Things every app shares

### Menu bar

The top 20 pixels are the menu bar. Click a title to open a pull-down. Click an item to run it. Click outside the menu, or press **Esc**, to dismiss it. Menu titles, items, and buttons are always **Chicago**.

### Typefaces

Cloister ships three 16×16 bitmap faces: **Chicago** (UI), **Geneva** (sans), and **Monaco** (mono). Quill and Tabula have **Font > Chicago / Geneva / Monaco** for document text only. Illumos, the picker, and every other control stay on Chicago. The faces live under `/sys/font/` and `resources/`; see [CFF.md](CFF.md).

### System menu (Esc)

When no dialog is open, **Esc** raises the system overlay titled **System Menu**:

| Button | Effect |
| --- | --- |
| **Continue** | Close the overlay and keep working |
| **Restart App** | Relaunch the current program from scratch |
| **Quit** | Halt the program (back to the picker, or out of Cloister) |

**Up / Down** move the default button. **Enter** activates it. **Esc** or a click outside **Continue**s. File > **Quit** in an app also halts, without this overlay.

While a file picker, confirm panel, the picker’s About box, or (in Tabula) a formula pass is running, Esc belongs to that dialog instead of the system menu.

### Open dialog

**File > Open** presents a System 6–style Standard File picker, centered on the window. It starts at `/`.

| Action | Result |
| --- | --- |
| Click a file, then **Open**, or press **Enter** | Open that file |
| Double-click a file (within 500 ms) | Open that file |
| Click / Enter a folder | Enter the folder |
| Click the path title above the list | Jump to a parent folder |
| **Cancel** or **Esc** | Dismiss without changing the document |

Hidden names (those starting with `.`) are not listed. The volume is labeled **NUX HD**. Folders you will see include **Apps**, **Lib**, **Docs**, **Src**, and **Resources**.

### Save As dialog

**File > Save As** presents the same folder list as Open, plus a **Save as:** name field and a **Save** button. The field is pre-filled with the current filename and selected, so the next character you type replaces it. You can create a new file; **Save** overwrites silently if that name already exists.

| Action | Result |
| --- | --- |
| Type a name, then **Save**, or press **Enter** | Write to that name in the current folder |
| Click a listed file | Copy its name into the field |
| Double-click a listed file | Save under that name |
| Click a folder | Enter the folder (the typed name is kept) |
| **Cancel** or **Esc** | Dismiss without changing the document or its path |

**Save** (the menu item) still writes the current path with no dialog.

### Unsaved changes

Quill, Tabula, Nib, and Easel ask before **File > New** if the document is dirty:

> Save changes?
>
> **Save** · **Don't Save** · **Cancel**

**Esc** is Cancel. **Open** and **Quit** do not ask; they proceed immediately. Illumos does not ask on New either.

### Snarf (clipboard)

Cut, Copy, and Paste go through `/sys/snarf`, Cloister’s clipboard. It lasts for the Cloister session, so you can copy in one app, return to the picker, launch another app, and paste. It does not survive quitting Cloister.

### Dirty marker

A dirty document is marked with an asterisk (`*`) next to the file name. Quill, Tabula, Nib, and Easel show it on the status bar. Illumos puts it in the window title (`Illumos *`). Nib also puts it in the window title (`Nib *`). Easel puts it in the window title (`Easel *`).

---

## 5. Quill — text editor

Quill is a proportional-font text editor. Menus and buttons stay **Chicago**. **Font > Chicago / Geneva / Monaco** sets the face for the document (body and hex). It opens `manuscript.quill` from the project root if that file exists.

```bash
./bin/cloister apps/Quill.bin
```

### Window

```
┌─────────────────────────────────────────────────────────┬──┐
│ File   Edit   View                                      │  │  menu bar
├─────────────────────────────────────────────────────────┤  │
│                                                         │▒ │
│  The text pane. Lines wrap to the pane width.           │▒ │  scrollbar
│  A red caret marks the insertion point.                 │▒ │
│                                                         │  │
├─────────────────────────────────────────────────────────┴──┤
│ manuscript.quill *  12:4                                   │  status
└────────────────────────────────────────────────────────────┘
```

The status bar shows the file’s basename, a `*` if unsaved, then **row:column** (1-based) of the caret.

### Menus

**File**

| Item | What it does |
| --- | --- |
| **New** | Empty buffer named `new.quill`. If the current file is dirty, the Save changes? panel appears first. |
| **Open** | Standard File picker. |
| **Save** | Write the current path. |
| **Save As** | Put-file picker: choose a folder and name, then write. |
| **Quit** | Halt Quill. |

**Edit**

| Item | What it does |
| --- | --- |
| **Cut** | Copy the selection to snarf, then delete it. |
| **Copy** | Copy the selection to snarf. |
| **Paste** | Insert snarf at the caret (replacing a selection). |
| **Select All** | Select the whole buffer. |

Cut, Copy, Paste, and Select All do nothing in hex view.

**View**

| Item | What it does |
| --- | --- |
| **Toggle Hex** | Check item. Switch between text and hex. |

**Font**

| Item | What it does |
| --- | --- |
| **Chicago** | Document face (default). Menus stay Chicago. |
| **Geneva** | Document sans |
| **Monaco** | Document mono |

### Typing and navigation

Printable characters insert at the caret. A selection is replaced by the new text.

| Key | Action |
| --- | --- |
| **Enter** | Insert a newline |
| **Tab** | Insert four spaces |
| **Backspace** | Delete the selection, or the character before the caret |
| **Left / Right** | Move by one character |
| **Up / Down** | Move by one visual line, keeping a target column |
| **Page Up / Page Down** | Jump by a screenful |
| **Home** | Start of the current visual line |
| **End** | End of the current visual line (before a trailing newline) |
| **Cmd/Ctrl+Home** | Start of the buffer |
| **Cmd/Ctrl+End** | End of the buffer |
| **Shift+arrows / Home / End / Page** | Extend or start a selection |
| **Cmd/Ctrl+C** | Copy |
| **Cmd/Ctrl+X** | Cut |
| **Cmd/Ctrl+V** | Paste |
| **Cmd/Ctrl+S** | Save |
| **Cmd/Ctrl+Shift+S** | Save As |
| **Cmd/Ctrl+A** | Select All |

Without Shift, a left/right move while a selection is active jumps to that end of the selection and clears it, rather than stepping one character.

### Mouse

- Click in the pane to place the caret.
- Drag to select (text view only).
- The vertical scrollbar on the right scrolls the pane.

Hex view does not drag-select. A click there jumps to the byte under the hex pair (the ASCII gutter is not a per-character hit target).

### Wrapping

Lines wrap to the pane width. If the overflowing line contains a space after the start of the line, Quill wraps at that last space. Otherwise it hard-wraps at the overflowing character. A single glyph wider than the pane still occupies its own line.

Wrapping is visual. Newlines in the file are the only hard breaks.

### Hex view

**View > Toggle Hex** shows the buffer as hex, 16 bytes per row:

```
0000  54 68 65 20 71 75 69 63 6B 20 62 72 6F 77 6E 20   The quick brown
0010  66 6F 78 2E                                          fox.
```

- Address is four hex digits.
- Bytes are uppercase hex pairs.
- The ASCII gutter on the right shows printable characters (` ` through `~`); everything else is `.`.
- The caret is a **hollow blue** box on the current nibble, with a matching box on that byte’s ASCII character.
- A short last row still occupies sixteen columns; bytes past the end of the file show as `00` / `.` so the ASCII column lines up.

To edit a byte, type hex digits `0`–`9`, `A`–`F` (either case). The first digit replaces the high nibble; the second replaces the low nibble and advances to the next byte. You cannot insert, delete, or grow the file in this view, and there is no edit at the end of the file. Ordinary typing, Tab, and Enter do nothing. Backspace only deletes if a selection is still active; toggling hex clears the selection, so Backspace is usually inert here.

Hex mode survives **File > New**. Uncheck **Toggle Hex** to return to text.

### Large files

Quill keeps a 64 KB window of the file in memory and a 1 MB working buffer. Moving the caret near either end of the window loads the next chunk. For everyday notes this is invisible. For files larger than 64 KB, scrolling to a distant region is what brings that region in.

### Startup and new files

| Situation | Path |
| --- | --- |
| Launch | `manuscript.quill` (empty buffer with that name if the file is missing) |
| File > New | `new.quill` |
| File > Open | the path you pick |
| File > Save | the current path |
| File > Save As | the path you name |

---

## 6. Illumos — font editor

Illumos edits Cloister Font Format (CFF) files: 256 glyphs, 1-bit pixels, proportional advance widths. The layout follows Turye-style font tools.

```bash
./bin/cloister apps/Illumos.bin
```

On launch it tries to open `resources/chicago12x12.cff`. If that file is missing or not a valid CFF length, it starts a blank 16×16 font named `untitled.cff`. Glyph **A** (code 65) is selected. The window title is **Illumos** or **Illumos \***; the filename is not shown there.

### Window

```
┌──────────────────────────────────────────────────────────────────┐
│ File   Edit   Glyph   Move                                       │
├───────────────┬──────────────────────────────────────────────────┤
│               │  A  [0x41]                                       │
│   magnified   │  ┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐              │
│   pixel       │  │ │ │ │ │A│ │ │ │ │ │ │ │ │ │ │ │  collection  │
│   editor      │  ├─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┼─┤  16×16 grid  │
│   with width  │  │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │ │  of all 256  │
│   rule        │  └─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘  glyphs      │
│               │                                                  │
│ A lazy dog. A quick fox. Jumping over.           pangram preview │
└──────────────────────────────────────────────────────────────────┘
```

- **Pixel editor** (left): one glyph, magnified. On pixels are black. A dashed baseline sits partway down the cell (row 6 of 8×8, row 11 of 16×16, row 18 of 24×24). A vertical **width rule** (dashed ticks) marks the advance width.
- **Hint** above the collection: the selected character, then its code in hex, e.g. `A [0x41]`.
- **Collection** (right): all 256 glyphs in a 16-wide grid, code 0 at top-left, 255 at bottom-right. The selected cell is inverted.
- **Pangram** (bottom): live preview of a sample sentence, drawn with the font you are editing.

### Menus

**File**

| Item | What it does |
| --- | --- |
| **New 8x8** | Blank 8×8 font, `untitled.cff` |
| **New 16x16** | Blank 16×16 font, `untitled.cff` |
| **New 24x24** | Blank 24×24 font, `untitled.cff` |
| **Open** | Standard File picker |
| **Save** | Write the current path |
| **Save As** | Put-file picker: choose a folder and name, then write |
| **Quit** | Halt Illumos |

New does not ask about unsaved work.

**Edit**

| Item | What it does |
| --- | --- |
| **Cut** | Snarf the current glyph’s pixels, then erase them |
| **Snarf** | Copy the current glyph’s pixels to the clipboard (Plan 9 wording for Copy). Width is not copied. |
| **Paste** | Replace the current glyph’s pixels if snarf holds exactly one glyph of this cell size. Width is left alone. |
| **Erase** | Clear the current glyph’s pixels (width is left alone) |

Paste only applies if the clipboard holds exactly one glyph of the current cell size. You cannot paste an 8×8 glyph into a 16×16 font.

**Glyph**

| Item | What it does |
| --- | --- |
| **Shift Up / Down / Left / Right** | Slide the bitmap inside the cell. Pixels that leave the cell are gone. |
| **Width −** | Decrease advance width by 1 (not below 0) |
| **Width +** | Increase advance width by 1 (not above the cell size: 8, 16, or 24) |

**Move**

| Item | What it does |
| --- | --- |
| **Up / Down / Left / Right** | Select a neighboring glyph. The collection wraps (byte-wise): Left of 0 is 255, Down from the last row wraps to the first. |

### Painting

| Mouse | Effect |
| --- | --- |
| Click a collection cell | Select that glyph |
| Left-drag in the editor, left of the width rule | Paint pixels on |
| Right-drag, Ctrl+left, or any non-left button in the editor | Erase pixels |
| Click or drag on/beyond the width rule | Set the advance width from that X position |

### Keyboard

| Key | Action |
| --- | --- |
| **Arrows** | Move the selection (same as Move menu) |
| **Shift+arrows** | Shift the bitmap (same as Glyph > Shift) |
| **[** | Width − |
| **]** | Width + |
| **Backspace** / **Delete** | Erase the current glyph (Cloister maps both keys to Backspace) |
| Printable keys | Append that character to the **pangram** (up to 120 characters). Typing does **not** select a glyph. |
| **Cmd/Ctrl+O** | Open |
| **Cmd/Ctrl+S** | Save |
| **Cmd/Ctrl+Shift+S** | Save As |
| **Cmd/Ctrl+X** | Cut |
| **Cmd/Ctrl+C** | Snarf |
| **Cmd/Ctrl+V** | Paste |

The pangram starts as:

> A lazy dog. A quick fox. Jumping over.

It resets on **New** and at launch, not on **Open**. It is a preview only and is not stored in the `.cff` file. There is no key that deletes pangram characters: Backspace erases the glyph instead.

A new blank font has every width at 0 except **space** (code 32), which is half the cell (4, 8, or 12).

### Cell sizes

Glyph size is taken from the file’s length, not from the name:

| Size | File length | Typical use |
| --- | --- | --- |
| 8×8 | 2,304 bytes | Small / icon-scale glyphs |
| 16×16 | 8,448 bytes | Body fonts (`chicago12x12.cff` is this size; “12×12” is cap height, not the grid) |
| 24×24 | 18,688 bytes | Display sizes |

See [CFF.md](CFF.md) for the on-disk layout.

---

## 7. Tabula — spreadsheet

Tabula is a sparse spreadsheet: columns **A–Z**, rows **1–99999**. Empty cells take no space on disk.

```bash
./bin/cloister apps/Tabula.bin
```

A new sheet is `untitled.tabula`, with **A1** active.

### Window

```
┌──────────────────────────────────────────────────────────────┐
│ File   Edit   Formula                                        │
├──────────────────────────────────────────────────────────────┤
│ A1  │ =B1+C1                                                 │  entry bar
├─────┬────────┬────────┬────────┬────────┬────────────────────┤
│     │   A    │   B    │   C    │   D    │ …                  │  column headers
├─────┼────────┼────────┼────────┼────────┼────────────────────┤
│   1 │  42    │   8    │   3    │  11    │                    │
│   2 │ hello  │        │        │        │                    │
│   3 │        │        │        │        │                    │
│  …  │        │        │        │        │                    │
├─────┴────────┴────────┴────────┴────────┴──────────────────┬─┤
│                                                            │▒│  v-scroll
├────────────────────────────────────────────────────────────┴─┤
│ untitled.tabula *  C3                                        │  status
└──────────────────────────────────────────────────────────────┘
                                  h-scroll along the bottom
```

- The **entry bar** shows the active cell’s name (A1, B12, …) and its **source**: the text you typed, including formulas starting with `=`.
- The **grid** shows computed values for formulas, and the typed text otherwise.
- Numbers (integers and decimals) are **right-aligned**. Text is **left-aligned**. Menus and headers stay **Chicago**. **Font > Chicago / Geneva / Monaco** sets the face for cell values and the entry bar.
- The active cell has a double black frame. A range selection is filled light blue.
- The **status bar** shows the file name, a `*` if dirty, and the active cell. During a formula pass it also says `Calculating... Esc to stop`. After you abort, it says `Stopped`.

About eleven columns and thirty rows fit on screen. Scrollbars, Page Up/Down, and moving the active cell bring the rest into view.

### Menus

**File**

| Item | What it does |
| --- | --- |
| **New** | Empty sheet named `untitled.tabula`. Dirty sheets get the Save changes? panel. |
| **Open** | Standard File picker. Does not ask about unsaved work. |
| **Save** | Write the current path as TABULA 400. |
| **Save As** | Put-file picker: choose a folder and name, then write. |
| **Quit** | Halt Tabula. |

**Edit**

| Item | What it does |
| --- | --- |
| **Cut** | Copy the selection as TSV, then clear those cells |
| **Copy** | Copy the selection as tab-separated rows |
| **Paste** | Paste TSV starting at the active cell |
| **Select All** | Select the bounding box of every used cell, or A1 if the sheet is empty |

**Formula**

| Item | What it does |
| --- | --- |
| **Calculate** | Recalculate every formula. Tabula also queues this automatically when a cell changes. |

**Font**

| Item | What it does |
| --- | --- |
| **Chicago / Geneva / Monaco** | Face for cell values and the entry bar. Menus and headers stay Chicago. |

### Entering values

Click a cell to select it. Click the **already-active** cell again (without dragging), or click the entry bar, to edit in place. Column and row headers are not click targets.

Start typing to **replace** the cell. The first printable character opens an edit and overwrites whatever was there. There is no in-cell caret: you only append and backspace, up to 63 characters. Shift+click does not extend the selection; use **Shift+arrows** or **drag**.

| Key | Action |
| --- | --- |
| **Enter** | Commit, move down one row |
| **Tab** | Commit, move right one column |
| **Esc** | Cancel the edit (restore the previous value) |
| **Backspace / Delete** | While editing: delete a character. Otherwise: clear the selection |
| **Arrows** | Commit, then move (Shift extends the selection) |
| **Page Up / Page Down** | Move by a screen of rows |
| **Home** | Column A of the current row |
| **End** | Column Z of the current row |
| **Cmd/Ctrl+C / X / V / S / A** | Copy / Cut / Paste / Save / Select All |
| **Cmd/Ctrl+Shift+S** | Save As |

Cell text is at most **63 characters**.

### What a cell is

Tabula classifies each cell from its text:

| You type | Type | Grid |
| --- | --- | --- |
| `hello` | string | left-aligned `hello` |
| `42` or `-7` | integer | right-aligned |
| `3.14`, `-0.5` | float (stored as typed) | right-aligned as typed |
| `3.`, `.5`, `3.14x` | string, not a float | left-aligned |
| `=B1+C1` | formula | last computed integer, or an error token |

A cell whose text starts with `=` is a formula, unless you forced it to a string (see [TABULA 400](#tabula-400-current)). Clearing a cell (empty edit, or Backspace on a selection) removes it.

### Formulas

Formulas are **integer arithmetic**. Type them with no spaces.

```
=A1+B1
=SUM(A1:A10)
=-(B2*3)+C1
=(A1+A2)/2
```

| Piece | Meaning |
| --- | --- |
| `+` `-` `*` `/` | Add, subtract, multiply, integer divide |
| `( )` | Grouping |
| leading `-` | Unary minus, as in `=-A1` |
| `A1` … `Z99999` | Cell reference (uppercase column, then row number) |
| `SUM(A1:B10)` | Sum of that inclusive rectangle. Corners may be in any order. Empty and string cells add 0; formula cells inside the range are evaluated. |

`SUM` must be uppercase, with two cell refs and a colon: `SUM(A1:B10)` works; `sum(A1:B10)`, `SUM(A1)`, and nested `SUM(SUM(…))` do not.

Division uses truncating integer division. Decimal literals in a formula (`=3.14`) are not numbers — they become `#NAME?`. A float **cell** used in arithmetic contributes only the integer prefix (`3.14` → `3`). Formula references are not rewritten on paste: `=A1` stays `=A1` even if you paste it into C3.

References to empty cells are 0. Strings used as operands yield `#VALUE!`.

### Error tokens

The grid shows these in place of a value. The entry bar still holds the formula source.

| Token | Meaning |
| --- | --- |
| `#DIV/0!` | Division by zero |
| `#VALUE!` | A string was used where a number was required |
| `#REF!` | Bad cell reference (row 0, row above 99999, column past Z) |
| `#CIRC` | The formula refers to itself, directly or through other formulas, or nesting is deeper than 64 |
| `#NAME?` | Unrecognized token, missing `)`, leftover characters, or a function other than `SUM` |
| `#STOP` | You pressed Esc during Calculate before this formula finished |

### Calculate

Changing a cell queues a recalculation. **Formula > Calculate** does the same.

A long pass shows `Calculating... Esc to stop` on the status bar. Mouse input is ignored until it finishes or you abort. **Esc** stops the pass: unfinished formulas become `#STOP`.

### Selection and clipboard

Click and drag to select a rectangle. Shift+click or Shift+arrows extend from the anchor.

Copy writes tab-separated values, rows separated by newlines — the **source** of each cell (so a formula copies as `=A1+B1`, not `11`). Paste starts at the active cell and fills right and down. Cells that would land past column Z or row 99999 are dropped. There is no Undo.

---

## 8. Nib — object drawing

Nib is a MacDraw-style drawing program: shapes are objects you can select, move, and resize, not pixels. Illumos remains the place to paint bitmap glyphs; Easel is the pixel painter.

```bash
./bin/cloister apps/Nib.bin
```

On launch it opens an empty page named `untitled.nib`. The window title is **Nib** or **Nib \***.

### Window

```
┌────────────────────────────────────────────────────────────┐
│ File   Edit   Arrange   Pen   Fill   View   Font           │  menu bar
├────┬───────────────────────────────────────────────────────┤
│ ▶  │                                                       │
│ /  │                                                       │
│ ▢  │                   drawing page                        │
│ ▢  │                                                       │
│ ○  │                                                       │
│ A  │                                                       │
├────┴───────────────────────────────────────────────────────┤
│ untitled.nib *                                             │  status
└────────────────────────────────────────────────────────────┘
```

- **Tool strip** (left): arrow, line, rectangle, rounded rectangle, oval, text. The current tool is inverted.
- **Page** (right): white drawing area. Optional 8-pixel grid.
- **Status**: file name, and `*` if dirty.

### Tools

| Tool | Mouse |
| --- | --- |
| **Arrow** | Click to select the frontmost object. Drag the body to move. Drag handles to resize. Click empty page to deselect. |
| **Line** | Drag endpoints. Shift constrains to 0° / 45° / 90°. |
| **Rectangle** | Drag a box. Shift makes a square. |
| **RoundRect** | Same, with System 6 corners. |
| **Oval** | Drag a box. Shift makes a circle. |
| **Text** | Click empty page to place a one-line label, then type. Click an existing label to edit it. |

Eight handles on rectangles, roundrects, and ovals; two endpoints on a line; a frame on text (move only).

### Menus

**File**

| Item | What it does |
| --- | --- |
| **New** | Empty page named `untitled.nib`. Dirty pages get the Save changes? panel. |
| **Open** | Standard File picker. Does not ask about unsaved work. |
| **Save** | Write the current path as NIB 1. |
| **Save As** | Put-file picker: choose a folder and name, then write. |
| **Quit** | Halt Nib. |

**Edit**

| Item | What it does |
| --- | --- |
| **Cut** | Copy the selection as NIB 1, then delete it |
| **Copy** | Copy the selection as NIB 1 |
| **Paste** | Paste NIB 1 objects, offset 8 pixels down-right |
| **Duplicate** | Copy in place, offset 8 pixels, and select the copy |
| **Clear** | Delete the selection |

**Arrange**

| Item | What it does |
| --- | --- |
| **Bring to Front** | Selected object draws last |
| **Send to Back** | Selected object draws first |

**Pen**

| Item | What it does |
| --- | --- |
| **1 pt / 2 pt / 3 pt** | Stroke width for the selection and for new shapes |

**Fill**

| Item | What it does |
| --- | --- |
| **None / White / Black** | Interior for closed shapes. Lines ignore fill. Frame is always black. |

**View**

| Item | What it does |
| --- | --- |
| **Grid** | 8-pixel light grid on the page |
| **Snap to Grid** | New shapes, moves, and handles quantize to 8 pixels |

**Font**

| Item | What it does |
| --- | --- |
| **Chicago / Geneva / Monaco** | Face for text objects. Menus and the tool strip stay Chicago. |

### Text

Click with the text tool, then type (up to 63 characters). **Enter** commits. **Esc** cancels: a brand-new empty label is deleted; an existing label is left as it was. Clicking elsewhere also commits.

### Keyboard

| Key | Action |
| --- | --- |
| **Arrows** | Nudge the selection 1 pixel (Shift = 8) |
| **Backspace** / **Delete** | Delete the selection. While editing text: delete a character. |
| **Enter** | Commit text |
| **Esc** | Cancel text. Otherwise the system menu, unless a dialog is open. |
| Printable | While editing: append |
| **Cmd/Ctrl+N / O / S** | New / Open / Save |
| **Cmd/Ctrl+Shift+S** | Save As |
| **Cmd/Ctrl+X / C / V / D** | Cut / Copy / Paste / Duplicate |

There is no Undo. **Open** and **Quit** do not ask about unsaved work.

### Startup and new files

| Situation | Path |
| --- | --- |
| Launch | `untitled.nib` (empty) |
| File > New | `untitled.nib` |
| File > Open | the path you pick |
| File > Save | the current path |
| File > Save As | the path you name |

---

## 9. Easel — pixel painting

Easel is a MacPaint-style painting program: you draw onto a bitmap, not onto objects. Nib remains the place to select and rearrange shapes.

```bash
./bin/cloister apps/Easel.bin
```

On launch it opens a blank 512×342 page named `untitled.eas`. The window title is **Easel** or **Easel \***.

### Window

```
┌────────────────────────────────────────────────────────────┐
│ File   Edit   Goodies                                      │  menu bar
├────┬───────────────────────────────────────────────────────┤
│ ✎ ░│                                                       │
│ ● ▓│                   512×342 page                        │
│ / ▢│                   on a dithered desk                  │
│ ○ *│                                                       │
├────┴───────────────────────────────────────────────────────┤
│ [pattern swatches]                                         │
├────────────────────────────────────────────────────────────┤
│ untitled.eas *  Pencil                                     │  status
└────────────────────────────────────────────────────────────┘
```

- **Tool palette** (left, two columns): pencil, eraser, brush, fill, line, rect, oval, spray. The current tool is inverted.
- **Page**: a white 512×342 bitmap, the original Macintosh screen size, sitting on a dithered desktop.
- **Patterns** (under the page): sixteen 8×8 MacPaint-style patterns. The current one is inverted.
- **Status**: file name, `*` if dirty, current tool, and `FatBits` when zoomed.

### Tools

| Tool | Mouse |
| --- | --- |
| **Pencil** | 1-pixel black. Right-click or Ctrl+left paints white. |
| **Eraser** | 8×8 white stamp. |
| **Brush** | 5×5 stamp of the current pattern. |
| **Fill** | Flood-fill the connected region of the same color with the current pattern. |
| **Line** | Drag endpoints. Shift constrains to 0° / 45° / 90°. |
| **Rect** | Drag a box. Shift makes a square. **Goodies > Filled** fills with the pattern, then frames in black. |
| **Oval** | Drag a box. Shift makes a circle. Filled works the same as Rect. |
| **Spray** | Scatter of the current pattern in a small radius. |

Pencil, eraser, brush, and spray interpolate as you drag so fast strokes do not skip pixels. Line, rect, and oval rubber-band until you release.

### Menus

**File**

| Item | What it does |
| --- | --- |
| **New** | Empty page named `untitled.eas`. Dirty pages get the Save changes? panel. |
| **Open** | Standard File picker. Does not ask about unsaved work. |
| **Save** | Write the current path as EAS1. |
| **Save As** | Put-file picker: choose a folder and name, then write. |
| **Quit** | Halt Easel. |

**Edit**

| Item | What it does |
| --- | --- |
| **Undo** | One level. A second Undo restores the stroke (MacPaint toggle). |
| **Invert** | Flip black and white on the whole page. |
| **Clear** | Erase the whole page to white. |

Undo, Invert, and Clear snapshot the page first (except Undo itself, which swaps). There is no selection marquee.

**Goodies**

| Item | What it does |
| --- | --- |
| **Grid** | 8-pixel dot grid on the 1× page |
| **FatBits** | 8× zoom of a 64×42 window into the page, with a cell grid. Arrow keys pan. |
| **Filled** | Rect and oval fill with the current pattern before framing |

### Patterns

Click a swatch under the page. **[** and **]** cycle. Pattern 0 is white, 1 is black (the default); the rest are checkers, lines, bricks, and hatch. Brush, fill, spray, and filled shapes use the current pattern. Pencil and shape frames stay black (or white, for a right-click pencil).

### Keyboard

| Key | Action |
| --- | --- |
| **Arrows** | Pan FatBits by 1 pixel |
| **[** **]** | Previous / next pattern |
| **Cmd/Ctrl+N / O / S** | New / Open / Save |
| **Cmd/Ctrl+Shift+S** | Save As |
| **Cmd/Ctrl+Z** | Undo |

**Open** and **Quit** do not ask about unsaved work.

### Startup and new files

| Situation | Path |
| --- | --- |
| Launch | `untitled.eas` (empty) |
| File > New | `untitled.eas` |
| File > Open | the path you pick |
| File > Save | the current path |
| File > Save As | the path you name |

---

## 10. Moving work between apps

Snarf is shared for the whole Cloister session.

Typical paths:

- **Quill → Tabula.** Copy a block of tab-separated lines in Quill, Quit to the picker, open Tabula, Paste at A1.
- **Tabula → Quill.** Copy a range; paste in Quill as TSV text.
- **Illumos glyphs.** Snarf copies raw glyph bytes, not a picture. Paste them into another glyph of the **same cell size**, in this or a later Illumos session of the same Cloister run.
- **Nib objects.** Copy writes a `NIB 1` fragment. Paste it in this or a later Nib session of the same Cloister run. Quill text will not paste as a shape.
- **Easel pixels.** Easel does not use snarf. Its page is a bitmap; Nib objects will not paint onto it, and Easel files will not open as Nib drawings.

Text copied from Quill will not paste as a glyph in Illumos, and glyph bytes pasted into Quill will look like binary.

---

## 11. File formats

All of these live on the host through Cloister’s file tree (`/sys/file/…`). Paths you pick in Open are ordinary files next to the project.

### Quill (`.quill`)

Plain text. Newlines are `\n`. No magic header. `manuscript.quill` in the repo is the default document.

### CFF (`.cff`)

256 width bytes, then 1-bit glyph tiles. Length selects 8×8, 16×16, or 24×24. Compatible with Uxn UFX (`.uf1` / `.uf2` / `.uf3`). Full layout: [CFF.md](CFF.md).

### TABULA 400 (current)

Saved by **File > Save**. First line is `TABULA 400`. Each following line is one used cell:

```
TABULA 400
A1,hello
B1,42
C1,3.14
D1,=B1+C1
E1,\=literally equals
F1,hello\, world
```

- Address, then a comma, then the **source** (not the computed value).
- Formulas are stored with a leading `=`.
- Inside values, backslash escapes: `\\`, `\t`, `\n`, `\r`, `\,`. A leading equals that is **not** a formula is stored as `\=`.

Opening a TABULA 400 file recalculates formulas.

### NIB 1

Saved by **File > Save**. First line is `NIB 1`. Each following line is one object:

```
NIB 1
rect 80 60 240 180 1 1
oval 300 80 420 200 0 1
line 40 40 200 120 0 2
text 90 70 90 70 0 1 Hello
```

- Kind (`line`, `rect`, `rrect`, `oval`, `text`), then `x0 y0 x1 y1 fill pen`.
- Fill is `0` none, `1` white, `2` black. Pen is `1`, `2`, or `3`.
- Text payload is the rest of the line after the six numbers. Backslash escapes: `\\`, `\n`, `\r`, `\s` (space).
- Unknown kinds are skipped. A file whose first line is not `NIB 1` is left unchanged.

### EAS1

Saved by **File > Save**. Binary, not text:

```
EAS1          4 bytes magic
u16le width   512
u16le height  342
packed bits   64 bytes per row, MSB first, 342 rows
```

Total size is 21,896 bytes. A file whose magic is not `EAS1`, or whose size is not 512×342, is left unchanged.

---

## 12. Limits and known behavior

| Limit | Where |
| --- | --- |
| 960×720, one program | Cloister |
| 1 MB buffer, 64 KB file window, 64 KB paste | Quill |
| 256 glyphs, widths 0…cell size | Illumos |
| Pangram 120 characters | Illumos |
| Columns A–Z, rows 1–99999 | Tabula |
| 8,192 filled cells | Tabula |
| 63 characters per cell | Tabula |
| 256 objects, 63 characters per label | Nib |
| 512×342, 1-bit, one undo | Easel |
| Integer formula arithmetic | Tabula |
| `SUM` is the only function | Tabula |
| No spaces inside formulas | Tabula |

**Open** and **Quit** never ask about unsaved changes. **File > New** asks in Quill, Tabula, Nib, and Easel. Illumos New replaces the font immediately.

Hex editing in Quill changes bytes in place. It does not insert or delete bytes, and Edit menu items are inert there.

Illumos **Save** writes the current path (`untitled.cff` after New, or the file you opened). **Save As** writes a new path through the put-file picker. Opening a file that is not a valid CFF length leaves the font unchanged but still records that path — a later Save can overwrite the file you just picked. New / Open / Quit do not warn about unsaved glyph work.

Tabula will not store an 8,193rd cell. Typing into a new address when the pool is full is ignored. There is no Undo.

Nib will not store a 257th object. A new shape when the pool is full is ignored. There is no Undo.

Easel’s canvas is fixed at 512×342. There is one level of Undo. Fill of a huge region can take a moment. FatBits shows a 64×42 slice of the page; arrow keys pan.

Paste in Quill that would not fit the buffer is refused, not truncated. Inserts past 1 MB are ignored.

---

## 13. Keyboard reference

Cmd means Command on macOS and Ctrl on other platforms. Either modifier works.

### Cloister / picker / dialogs

| Key | Context | Action |
| --- | --- | --- |
| **Esc** | App, no dialog | System Menu |
| **Esc** | Picker, About open | Close About Cloister |
| **Esc** | Open dialog | Cancel (or close the folder popup) |
| **Esc** | Save changes? | Cancel |
| **Esc** | Tabula, editing | Cancel the edit |
| **Esc** | Tabula, calculating | Stop the pass |
| **Esc** | Nib, editing text | Cancel the label |
| **Enter** | Picker, About closed | Launch the highlighted program |
| **Enter** | Picker, About open | Close About Cloister |
| **Enter** | Open dialog | Open the highlighted name |
| **Enter** | System Menu | Activate the highlighted button |
| **Arrows** | Picker, Open, System Menu | Move the highlight |

### Quill

| Key | Action |
| --- | --- |
| Printable | Insert |
| **Enter** | Newline |
| **Tab** | Four spaces |
| **Backspace** | Delete selection or previous character |
| **Arrows / Page / Home / End** | Move (Shift extends) |
| **Cmd+Home / Cmd+End** | Buffer start / end |
| **Cmd+C / X / V / S / A** | Copy / Cut / Paste / Save / Select All |
| Hex digits in hex view | Edit the current byte |

### Illumos

| Key | Action |
| --- | --- |
| **Arrows** | Select neighboring glyph |
| **Shift+arrows** | Shift pixels |
| **[** **]** | Width − / + |
| **Backspace** / **Delete** | Erase glyph |
| Printable | Append to pangram (does not select a glyph) |
| **Cmd+O / S / X / C / V** | Open / Save / Cut / Snarf / Paste |

### Tabula

| Key | Action |
| --- | --- |
| Printable | Replace / extend the edit |
| **Enter** | Commit, down |
| **Tab** | Commit, right |
| **Esc** | Cancel edit, or stop Calculate |
| **Backspace** | Edit delete, or clear selection |
| **Arrows / Page / Home / End** | Commit and move (Shift extends) |
| **Cmd+C / X / V / S / A** | Copy / Cut / Paste / Save / Select All |

### Nib

| Key | Action |
| --- | --- |
| Printable | Append while editing a label |
| **Enter** | Commit text |
| **Esc** | Cancel text |
| **Backspace** / **Delete** | Delete selection, or a character while editing |
| **Arrows** | Nudge 1px (Shift = 8) |
| **Cmd+N / O / S** | New / Open / Save |
| **Cmd+C / X / V / D** | Copy / Cut / Paste / Duplicate |

### Easel

| Key | Action |
| --- | --- |
| **Arrows** | Pan FatBits |
| **[** **]** | Previous / next pattern |
| **Cmd+N / O / S** | New / Open / Save |
| **Cmd+Z** | Undo |

---

## See also

- [README](../README.md) — building NUX, Lux, and Cloister
- [ui.md](ui.md) — the shared control toolkit these apps are built on
- [CFF.md](CFF.md) — Cloister Font Format
- [using-lux.md](using-lux.md) — writing your own Cloister programs
