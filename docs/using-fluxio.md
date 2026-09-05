# Using Fluxio

This guide covers the fluxio compiler workflow: compiling `.fx` source to `.bin` bytecode and running it. For the language itself, see [`fluxio_tutorial.md`](fluxio_tutorial.md).

## Overview

Fluxio is a second, C-like language for the NUX VM, alongside Lux (the Forth-style concatenative language — see [`using-lux.md`](using-lux.md)). Both compile to the same 55-opcode bytecode and run on the same `nux`/`cloister` hosts; they differ only in front end.

| Command    | Role                                                          |
| ---------- | -------------------------------------------------------------- |
| `fluxioc`  | Compiles `.fx` source into `.bin` bytecode.                   |
| `nux`      | Console runner. Executes `.bin` and prints to stdout.         |
| `cloister` | Graphical OS. Executes `.bin`, with windowing/input/draw.     |

```
  hello.fx ──(fluxioc)──▶ hello.bin ──(nux | cloister)──▶ output
```

`cloister` also accepts a `.fx` path directly and compiles it in-process, the same shortcut it has long offered for `.lux` (`src/cloister.c`; the pipeline itself is `fx_build_image()` in `src/fluxio_build.c`). That covers the link step too — an app declaring `extern`s gets the Lux UI/SF library merged in on the fly, exactly as `fluxlink` would (see [On-the-fly `.fx` launches](#on-the-fly-fx-launches) below). `bin/nux` does **not**: it still only recognizes a `.lux` suffix, so headless Fluxio programs go through `fluxioc` to a `.bin` first.

`make apps` compiles `apps/*.lux` to `apps/*.bin` and `apps/fluxio/*.fx` to `apps/fluxio/*.bin` (graphical). `make all` builds the tools and then `apps`, so the two stay in sync. Cloister's picker lists sources in both columns — `apps/*.lux` on the left, `apps/fluxio/*.fx` on the right — and compiles whichever you pick at launch. The `.bin` files remain what you ship and what `bin/nux` runs.

## Building the tools

```bash
make
```

Produces `bin/fluxioc` along with `bin/nux`, `bin/luxc`, `bin/cloister`, `bin/luxrepl`, and the test binaries (see `TARGETS` in `Makefile`). To build just the Fluxio compiler:

```bash
make bin/fluxioc
```

Run the Fluxio compiler's own test suite (parser/codegen/VM-level assertions, including a pixel-exact draw test):

```bash
make test           # runs test_vfs, test_vm, test_compiler, test_fluxio_compiler
```

## `fluxioc` — the command-line compiler

### Synopsis

```
fluxioc [-o out.bin] [-target graphical|headless] [-dumpAt 0xADDR] [-dumpRange N] <file.fx>
```

Source: `src/fluxioc.c`.

### Flags

| Flag              | Default      | Purpose                                                                 |
| ----------------- | ------------ | ------------------------------------------------------------------------ |
| `-o <path>`       | `<input>.bin`| Write bytecode to this path (strips a trailing `.fx`, then appends `.bin`). |
| `-target <t>`     | `graphical`  | Base load address: `headless` (for `nux`) or `graphical` (for `cloister`). |
| `-dumpAt 0xADDR`  | —            | Instead of writing a file, disassemble a window of bytecode around this address and print it (hex + best-effort disassembly), then exit. |
| `-dumpRange N`    | 64           | Half-width (in bytes either side of `-dumpAt`) of the disassembly window. |

The two base addresses matter because `nux` and `cloister` load programs at different fixed base addresses (`HEADLESS_BASE_ADDRESS` / `GRAPHICAL_BASE_ADDRESS`, `include/opcodes.h`); a `.bin` compiled for the wrong target will jump to the wrong absolute addresses. Compile for `headless` to run under `bin/nux`, `graphical` to run under `bin/cloister`.

### Examples

Compile for the console runner:

```bash
./bin/fluxioc -target headless -o examples/fluxio/fib.bin examples/fluxio/fib.fx
./bin/nux examples/fluxio/fib.bin
```

Compile for the graphical host:

```bash
./bin/fluxioc -target graphical -o examples/fluxio/hello_cloister.bin examples/fluxio/hello_cloister.fx
./bin/cloister examples/fluxio/hello_cloister.bin
```

Programs that only touch `emit`/`print` (no `vfs_open`/draw calls) behave identically under either target — but the target must still match whichever host actually runs the `.bin`, since it's baked into every absolute address in the bytecode.

Inspect the compiled bytecode around a specific address (useful when a VM fault reports a PC):

```bash
./bin/fluxioc -dumpAt 0x10120 -dumpRange 32 examples/fluxio/fib.fx
```

### Multi-file programs

`include "path.fx";` splices another file's tokens in place, resolved relative to the *including* file's directory. `fluxioc` handles this transparently — pass only the entry file:

```bash
./bin/fluxioc -target headless -o examples/fluxio/include_demo.bin examples/fluxio/include_demo.fx
./bin/nux examples/fluxio/include_demo.bin
```

See `examples/fluxio/include_demo.fx` + `examples/fluxio/include_lib/mathlib.fx`.

## Fluxio and Cloister

Programs that call `vfs_open("/dev/draw")` and friends reach the same windowing/input surface Lux apps use (see [`ui.md`](ui.md)), just through a curated set of builtins instead of the `SCI`/`DRAW` modules. See [`fluxio_tutorial.md`](fluxio_tutorial.md#cloister-builtins) for the full list.

### On-the-fly `.fx` launches

```bash
./bin/cloister apps/fluxio/Snake.fx     # compile and run, no .bin needed
./bin/cloister apps/fluxio/Quill.fx     # compiles *and* links, same as the Makefile
./bin/cloister apps/fluxio/Quill.bin    # run the prebuilt image
```

What `cloister` does with a `.fx` path is the whole `fluxioc` pipeline in memory — includes, parse, the `version <n>;` check, codegen — plus, if the program declares any `extern`s, the `fluxlink` merge on top. Externs are the signal that a link is needed: they bind to fixed addresses in the trampoline table, which only exists once the library is merged in. An app with no externs runs straight out of codegen.

The linked case reads `lib/uisf.bin`, `lib/uisf.symtab.json`, and `abi/uisf.exports.json` relative to the current directory, so run `cloister` from the repo root (it already resolves `apps/Picker.lux` that way). Those are build artifacts — if they're missing, `cloister` says so and points at `make uilib`. The append-only export check stays a build-time concern and is *not* repeated here: it belongs in `fluxlink`, where a violation is a change someone is making, not a gate that could reject a legitimate image at launch.

An in-process build is byte-identical to what `fluxioc` + `fluxlink` write out — `test_abi_conformance` checks every shipped `.fx` against its committed `.bin` on each `make test`, so source launches and `.bin` launches can't drift.

### The Esc system menu

Every graphical Fluxio app should include the shared pause/quit sheet, so Esc does the same thing across the machine as it does in Lux apps (`APP::loop`'s Continue / Restart App / Quit overlay):

```c
include "../../lib/escape_menu.fx";
...
escmenu_init(canvas_w, canvas_h);              /* once, after canvas_size() */

int got_k = poll_kbd(kfd);
while (got_k) {                                 /* drain, don't poll once */
    if (!escmenu_kbd(kbd_type(), kbd_key())) {
        /* the app's own key handling */
    }
    got_k = poll_kbd(kfd);
}
...
escmenu_draw(fd);                               /* last, so it sits on top */
if (escmenu_wants_quit()) { break; }
if (escmenu_wants_restart()) { /* reset */ escmenu_ack_restart(); }
```

Two things are easy to get wrong, and both produce a menu that *looks* like it does nothing at all:

- **Feed keys to `escmenu_kbd(kbd_type(), kbd_key())`, not `escmenu_key(kbd_key())`.** `/dev/kbd` delivers a `KEY_UP` (type 1) after every `KEY_DOWN` (type 0), and Esc *toggles* — so handing the library both halves of one physical keypress opens the sheet on the down and shuts it again on the up, and it never survives to be drawn. `escmenu_kbd` filters the type for you; `escmenu_key` is the raw keydown-only entry point.
- **Drain the input queue each frame** with a `while`, not a single `if`. Every keypress is two events, so a one-event-per-frame poll runs a frame behind and falls further behind under any real burst of input.

Both `escmenu_kbd` and `escmenu_mouse` return 1 for anything they consumed, which is what the `if (!...)` gate above is for: while the sheet is up it owns the keyboard and mouse, and the app underneath sees nothing. Give it precedence over the app's own keys — but *below* any dialog the app already has open, since Esc belongs to the frontmost dialog (`Quill.fx`'s chain is confirm panel → file picker → sheet → its own keys).

The sheet is a hand port of `APP::draw-esc-popup` and the `UI::overlay-button` / `UI::button-draw` stack it calls, so it behaves like the Lux overlay and not just like a panel with three rectangles on it:

- Buttons are System 6 rounded rects (`DRAW::paint-rrect-6` / `frame-rrect-6`, radius 6), and the default ring is three nested rounded frames.
- Labels are centred on their measured `DRAW::str-w` width at `h/2 - 7` from the button top.
- The default ring **follows the mouse** (`UI::overlay-pick`), not just Up/Down.
- A click posts on *release inside the button it was pressed in* (`UI::button-update`'s `WAS_DOWN` rule), and the face inverts while held — dragging off a button cancels it.
- A button-down anywhere outside the buttons Continues, per [`user-manual.md`](user-manual.md)'s "System menu (Esc)".

That equivalence is pinned, not asserted: `test_escape_menu_matches_lux_pixel_for_pixel` (`src/test_fluxio_compiler.c`) boots a real Lux app, presses Esc, renders the Fluxio sheet, and compares the whole 204×164 panel region pixel by pixel on every `make test`. It is what caught the first version's square buttons and guessed label offsets — 938 differing pixels.

The sheet draws with `fill_rect`/`draw_str` only and no `extern`s, so including it never turns an unlinked app into a linked one. It also doesn't select a font: an app that changes faces should set its own back to Chicago (face 0) before `escmenu_draw`, the way `lib/app.lux`'s `draw-esc-popup` does.

### Declaring a window size

`cloister` sizes its window from the app's own source when the app says what it wants — the Fluxio spelling of Lux's `@WIN_W` / `@WIN_H` constants is a pair of top-level globals:

```c
int win_w = 640;
int win_h = 480;
```

Read straight out of the source text before compiling (`scan_fx_win_size` in `src/cloister.c`), so it works whether you launch the `.fx` or its sibling `.bin` — a `.bin` carries no such metadata, so `cloister` looks next to it for the source it was built from. A `--width` / `--height` flag overrides it per dimension; an app that declares neither gets the 960×720 default. Values outside 100–4000 are ignored.

A `.bin` built this way runs fine headlessly too — `machine_create()` wires up the same System/VFS/draw device for both `nux` and `cloister`, so draw calls execute (and were verified pixel-exact against the framebuffer) even with no window to look at. The difference is pacing: `bin/nux` is a one-shot runner that stops at the *first* `yield()` rather than pumping a frame loop, so a Fluxio program with a `while` frame loop draws exactly one frame under `nux` and then exits. Run it under `bin/cloister` to see it animate.

## Troubleshooting

- **"Lexing failed" / "Parsing failed" / "Compilation failed"** — `fluxioc` prints one of these three fixed messages and exits 1; the specific error already went to stderr above it (unknown identifier, arity mismatch, naming-convention violation, missing doc comment, etc.). There's no `-trace` flag for Fluxio the way `luxc` has one — errors are reported at the point of failure, not accumulated.
- **"struct/function declaration must be preceded by a /** ... */ doc comment"** — every top-level `func_decl` and `struct_decl` needs an immediately preceding `/** ... */` block comment. See [`fluxio_tutorial.md`](fluxio_tutorial.md#naming-and-documentation-rules).
- **"missing required 'version <n>;' directive"** — every program needs a top-level `version <n>;` (Kelvin versioning, see `AGENTS.md`) somewhere in the main file or an `include`d one. Unconditional — unlike `luxc`, `fluxioc` has no library-build mode to exempt (Fluxio only produces standalone apps; it links against already-compiled Lux libraries via `fluxlink` instead of compiling `.fx` in a library mode).
- **"(must be lower_snake_case)" / "(must be UpperCamelCase...)"** — functions, variables, params, and struct fields must be `lower_snake_case`; struct type names must be `UpperCamelCase`. Not a lint warning — a compile error.
- **Recursion rejected at compile time** — a function that calls itself (directly or through a cycle) must be declared `recursive(N) int f(...)` with a literal bound `N`. Plain functions can't recurse at all. See [`fluxio_tutorial.md`](fluxio_tutorial.md#recursion).
- **Bytecode runs but halts with an unexpected negative return** — `-1` is the recursion-depth-exceeded sentinel, `-2` is an out-of-bounds array/index access. Both halt cleanly rather than corrupting memory.
- **"declares externs, so it needs the Lux UI/SF library linked in"** — launching a `.fx` with `extern` declarations straight from source needs `lib/uisf.bin` and its symbol table, which are build artifacts. Run `make uilib` (or `make apps`), from the repo root.
- **A string argument "must be a string literal"** — `vfs_open`, `set_window_title`, and `draw_str`'s text argument only accept a literal `"..."` at the call site, never a computed/runtime value. Fluxio has no general string-as-value type yet.

## Further reading

- [`fluxio_tutorial.md`](fluxio_tutorial.md) — the language itself: types, control flow, arrays, structs, includes, Cloister builtins.
- [`fluxio-language-plan.md`](fluxio-language-plan.md) — design history and rationale for each language slice (v1 → v2d), including the JSF AV safety-discipline mapping.
- [`using-lux.md`](using-lux.md) — the sibling Forth-style language and its CLOISTER workflow.
- [`opcodes.md`](opcodes.md) — bytecode opcode reference (shared by both languages).
- [`NUX_ARCHITECTURE.md`](NUX_ARCHITECTURE.md) — VM internals and memory map.
