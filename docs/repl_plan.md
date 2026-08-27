# Cloister app: a Lux REPL

A System 6 guest ROM (`apps/Repl.lux`) that is the graphical counterpart of `bin/luxrepl`. Pick it from Cloister (or `./bin/cloister apps/Repl.lux`) and type Lux at a `lux>` prompt.

This is a **guest app**, not a host-side mode of `cloister.c`. The picker stays the no-argument Cloister UI. Stale docs that describe a built-in graphical REPL in the host (`docs/using-lux.md`) will be corrected, not resurrected.

**Memory:** every guest address and every C load address comes from `include/memory_map.h`. No new ad-hoc band, no `0xA00000` transcript (that is `MM_LUX_HEAP_BASE`, owned by `lib/mem.lux`). Lux cannot include the C header; the app cites the macro names in comments next to hex literals, same as `apps/Quill.lux` and `lib/mem.lux`.

## Current state

| Piece | What it is today |
|---|---|
| `src/repl.c` / `bin/luxrepl` | C CLI. Persistent stack + `@word` history. Each line is `history + stack literals + line`, then `compile_source` + a fresh VM. |
| `src/cloister.c` | SDL host. Boots `apps/Picker.lux` with no args, or run one ROM. No REPL. |
| `src/compiler.c` | `compile_source` returns bytecode or NULL. Errors go to **stderr** only (`Unknown word`, `Unclosed quotation`, …). |
| `src/vm.c` | PC **must** stay in `[image_base, image_end)`. Writes into the image fault. You cannot stash bytecode in RAM and `JMP` to it. |
| `include/memory_map.h` | Authoritative 32MB partition. `HEADLESS_BASE_ADDRESS` / `GRAPHICAL_BASE_ADDRESS` in `vm.h` are aliases of `MM_HEADLESS_CODE_BASE` / `MM_GRAPHICAL_CODE_BASE`. |
| `AGENTS.md` | No new opcodes. Cloister is a single-app fantasy machine. Apps look like Macintosh System 6. |
| `lib/ui.lux` + `lib/app.lux` | Buttons, menus, scrollbars, `SF::` file picker. **No text field.** |
| `/sys/vm/new` | Spawns a **child Machine** from a file path. Graphical-host policy: do not turn this into rio. |

`luxrepl` already has the right evaluation model. The work is: share that engine, expose it as VFS files, and put a System 6 skin on it whose RAM sits in the named bands.

## Goals

- A pickable Cloister app that matches `luxrepl` semantics: persistent stack, persistent word definitions, the same meta-commands (`help`, `.s`, `words`, `history`, `clear` / `reset`, `clearstack` / `cs`, `drop`).
- Line editor: printable keys, backspace, left/right, home/end, up/down command history, Return to eval.
- Transcript pane (input + stack dump + `emit`/`.` output + errors) and a live stack strip.
- File / Edit menus: load/save definition history via `SF::`, copy/paste via `/sys/snarf`.
- Guest RAM only in `MM_APP_SMALL_STATE_*` (cells) and `MM_APP_BULK_BUFFER_*` (transcript, input, scratch). Eval sidecar loads at `MM_HEADLESS_CODE_BASE`. Restart vector is `MM_GRAPHICAL_CODE_BASE`.
- No new opcodes. No window manager. No execute-outside-image VM change. No new memory-map band unless a named band truly does not fit (it does).

## Non-goals

- Evaluating into the **running Repl image** (Smalltalk-style live coding of the app itself).
- Letting eval'd lines call Cloister devices (`/dev/draw`, `/dev/mouse`, `APP::…`). Eval is headless, same as `luxrepl`.
- A self-hosted Lux interpreter written in Lux.
- Replacing the Cloister picker with the REPL.
- A Fluxio REPL.
- Multi-line word definitions in one go beyond “the line must contain `;`” (same as `luxrepl`).
- Adding `UI::field` to the toolkit in v1.
- Inventing a Repl-specific band in `memory_map.h`. The existing app-small-state and app-bulk-buffer bands are what those comments describe.

## Why eval cannot live in the guest

```
Repl.lux  ──compile──►  image at MM_GRAPHICAL_CODE_BASE (0x600000, execute-only)
                              │
                              │  guest cannot compile Lux
                              │  guest cannot JMP to writable RAM
                              ▼
                     /sys/lux/eval  (host)
                              │
                              ▼
                     scratch VM (headless, MM_HEADLESS_CODE_BASE = 0x011000)
                     compile_source(history + stack + line)
                     vm_run until HALT or cycle cap
                              │
                              ▼
                     stack / stdout / error copied back to VFS files
```

The compiler is C. The guest image cannot grow. So the host compiles and runs a **sidecar** VM; the Repl ROM only owns the screen and talks to files.

`/sys/vm/new` is the wrong primitive: it launches a child **graphical** machine from a path. Eval is a computation, not a ROM.

## Memory map (required)

Source of truth: `include/memory_map.h`, explained in `docs/memory-map.md`. Lux still hand-authors hex; C includes the header. `luxc` warns on duplicate `@NAME 0xHEX ;` values (not on span overlap).

Repl **includes** `lib/app.lux`, `lib/ui.lux`, `lib/sf.lux`, `lib/draw.lux` (and thus their small-state). Those library ranges are taken. Repl’s own cells and buffers go in the leftover slices of the two app bands.

### Occupied by libraries Repl will link (do not reuse)

| Range | Owner | Band |
|---|---|---|
| `0x8B0000`–~`0x8B2680` | `lib/sf.lux` | `MM_APP_SMALL_STATE` |
| `0x8C0000`–~`0x8C0080` | `lib/app.lux` (`APP::modal-f` shared with `UI::APP_MODAL` on purpose) | `MM_APP_SMALL_STATE` |
| `0x8E0000`–~`0x8E3100` | `lib/ui.lux` (and `lib/cff.lux` if pulled) | `MM_APP_SMALL_STATE` |
| `0x8F0000`, `0x8F8000` | `lib/draw.lux` `BUF` / font state | `MM_APP_SMALL_STATE` |
| `0xA00000`–`0xC00000` | `lib/mem.lux` heap (`MM_LUX_HEAP_*`). **Forbidden.** | heap |

`0x8A0000` (Snake / UIDemo) and `0x8D0000` (Calculator) are other-app small-state. Cross-app collisions are inert today (one ROM per VM) but Repl still picks its **own** unused slice so the map stays readable and `luxc`’s duplicate-const warning stays quiet if those apps are ever compiled into one unit.

### Repl layout

**Small state** — `MM_APP_SMALL_STATE_BASE` .. `MM_APP_SMALL_STATE_END` (`0x800000`–`0x900000`). Cells only, not buffers.

```
0x880000  REPL_STATE      ~0x200 bytes of 4-byte cells
            eval-fd stack-fd words-fd hist-fd ctl-fd error-fd out-fd snarf-fd
            caret in-len hist-n hist-i scroll dirty in-sel-a in-sel-b
            (document each cell in apps/Repl.lux header comments)
```

`0x880000` is unused by every current `lib/*.lux` and `apps/*.lux`. Keep the whole block under `0x880200` so it cannot grow into SF (`0x8B0000`).

**Bulk buffers** — `MM_APP_BULK_BUFFER_BASE` .. `MM_APP_BULK_BUFFER_END` (`0x900000`–`0xA00000`). Sized so the last byte is `< 0xA00000`.

```
0x900000  TRANSCRIPT      0x20000   (128 KB append-only text)
0x920000  INPUT_LINE      0x400     (1024 B, current line + NUL)
0x920400  CMD_HIST        0x4000    (16 KB ring of previous input lines)
0x924400  EVAL_SCRATCH    0x2000    (8 KB, last /sys/lux/eval read)
0x926400  STACK_SCRATCH   0x400     (binary stack snapshot)
0x926800  PATH_BUF        0x200     (SF open/save path)
0x926A00  (end of Repl bulk; 0x926A00 < MM_APP_BULK_BUFFER_END)
```

Quill and Illumos also use `0x900000` in *their* images (font / file buffers). That is the documented inert cross-app overlap. Repl must not include those apps.

If a buffer later needs more than this slice, **grow inside `MM_APP_BULK_BUFFER_*`**, or add a new named band to `memory_map.h` with a comment. Do not slide into `MM_LUX_HEAP_BASE`.

### C-side addresses (`#include "memory_map.h"`)

| Use | Macro | Notes |
|---|---|---|
| Eval `compile_source` / `vm_create` base | `MM_HEADLESS_CODE_BASE` | Same as `HEADLESS_BASE_ADDRESS`. Not a raw `0x11000`. |
| Eval VM RAM size | `4 * 1024 * 1024` | Enough for headless image + data. Not 32MB per eval. |
| Guest ROM load / restart `JMPSTACK` | `MM_GRAPHICAL_CODE_BASE` | Already `APP::esc-restart`’s `0x600000`; Repl comments it as the macro. |
| Device ports (unchanged) | `MM_DEVICE_BASE` | Via existing SCI in `lib/vfs.lux`. |

`lux_eval.c` includes `memory_map.h` directly (or via `vm.h`) and never writes a bare load address.

### Docs / header touch-ups

- `include/memory_map.h` app-small-state comment lists Snake, UIDemo, SF, app, Calculator, ui, cff, draw, Illumos — add **Repl**.
- `docs/memory-map.md`: one row or bullet for Repl’s `0x880000` cells and `0x900000` bulk, pointing at `apps/Repl.lux`.

## Proposed design

### 1. Shared eval engine (`src/lux_eval.c`, `include/lux_eval.h`)

Lift the session logic out of `src/repl.c` so the CLI and the VFS device are the same program.

```c
typedef struct {
    int32_t stack[MAX_STACK_SIZE];
    int stack_count;
    char history[16384];          /* accumulated @word defs + INCLUDE lines */
    char definitions[128][64];
    int def_count;
    char last_out[4096];
    char last_error[512];
} LuxSession;

typedef enum {
    LUX_EVAL_OK,
    LUX_EVAL_DEFINED,    /* @name ... ; recorded, not executed */
    LUX_EVAL_META,       /* help / .s / words / … handled here */
    LUX_EVAL_COMPILE,    /* last_error set */
    LUX_EVAL_RUNTIME,    /* last_error set */
    LUX_EVAL_LIMIT       /* cycle cap; stack still copied */
} LuxEvalStatus;

LuxEvalStatus lux_eval_line(LuxSession* s, const char* line);
void lux_eval_reset(LuxSession* s);        /* clear history + words, keep stack */
void lux_eval_clearstack(LuxSession* s);
```

Evaluation (same as today’s `evaluate()` in `repl.c`):

1. Meta-commands are intercepted **before** compile (`help`, `?`, `clear`, `reset`, `clearstack`, `cs`, `stack`, `.s`, `drop`, `words`, `history`). `exit` / `quit` / `q` are **not** host meta in the app — Esc already raises Continue / Restart / Quit; the app intercepts `exit` and `HALT`s.
2. A line starting with `@` that contains `;` is appended to `history` and the word name list. It is not executed (errors surface on first use), matching `luxrepl`.
3. Otherwise build `history + "<stack values as decimals> " + line`, `compile_source(..., MM_HEADLESS_CODE_BASE, ...)`.
4. Run in a **fresh** `vm_create(..., MM_HEADLESS_CODE_BASE, 4MB, ...)`. Capture `OP_OUT` via `output_handler` into `last_out`. Copy `vm->stack[0..stack_ptr)` back into the session.
5. Cycle cap (e.g. 1e6 ticks). Infinite loops return `LUX_EVAL_LIMIT` and do not freeze Cloister’s frame loop. `YIELD` in eval is treated as halt-and-return, not a request for another frame.

`bin/luxrepl` becomes a thin stdin loop over `LuxSession`. Behavior of the CLI must stay byte-for-byte on the existing command set.

`INCLUDE "path"` on its own line is recorded into `history` like a definition so later evals see those words. It is not a meta-command that bypasses the compiler.

### 2. Compiler errors as data

Today `compile_source` prints and returns NULL. The graphical app has no stderr.

- Add a compiler-struct (or process-local) error buffer. All `fprintf(stderr, "Unknown word…")` sites in `compiler.c` / include handling also `compiler_errorf(...)`.
- `const char* compiler_last_error(void)` for the eval engine to copy into `LuxSession.last_error`.
- Keep printing to stderr so `luxc` / `nux` traces stay the same.
- Tests in `src/test_compiler.c`: unknown word, unclosed quotation, unclosed `{` frame — assert the string, not just NULL.

### 3. VFS device `/sys/lux/…` (and `/dev/lux/…` via the existing `/dev/` → `/sys/` alias)

One `LuxSession` per `System`, allocated lazily on first open, freed in `system_free` / `vfs_state_free`. The Repl app is the only client in v1; the session dies with the ROM.

| Path | Write | Read |
|---|---|---|
| `/sys/lux/eval` | NUL-terminated or raw line (trim trailing `\n`). Runs `lux_eval_line`. | Last result text: `OK\n` / `DEFINED <name>\n` / `ERROR <msg>\n` / `LIMIT\n` plus any `last_out`. |
| `/sys/lux/stack` | — | Big-endian `int32 count` then `count` big-endian `int32` values (matches `LOADI`). |
| `/sys/lux/words` | — | Space-separated names, no trailing junk. Empty if none. |
| `/sys/lux/history` | Replace definition history (used by File → Open). | Raw history buffer. |
| `/sys/lux/ctl` | `reset\n`, `clearstack\n`. | — |
| `/sys/lux/error` | — | `last_error` or empty. |
| `/sys/lux/out` | — | captured `OP_OUT` from the last eval. |

Write to `eval` is the only “do work” operation. Reads are snapshots. Seek is ignored (or 0); each open starts at offset 0, like `/sys/snarf`.

Guest bindings go in `lib/lux.lux` (`MODULE LUX`): `LUX::eval`, `LUX::stack@`, `LUX::words`, `LUX::history@`, `LUX::history!`, `LUX::reset`, `LUX::clearstack`. Built on `lib/vfs.lux` the same way `DRAW` wraps `/dev/draw`. Path constants only; **no RAM globals** in this library (avoids eating small-state). Scratch for a call lives in the caller (Repl’s `EVAL_SCRATCH`).

Tests in `src/test_vfs.c`:

- `5` then `10` then `+` → stack `[15]`.
- `@double dup + ;` then `21 double` → `[42]`.
- Unknown word → `ERROR` + nonempty `/sys/lux/error`, stack unchanged.
- `clearstack` via ctl; `reset` drops words.
- History write then eval of a stored word.
- Cycle-cap: a tight recursive word does not hang the test.

### 4. The app (`apps/Repl.lux`)

Pattern: `apps/Calculator.lux` / `apps/UIDemo.lux` — `INCLUDE "lib/app.lux"`, `UI::` menu bar, `APP::loop`.

File header comments name every address with the `MM_*` band, mirroring Quill’s memory-layout block.

**Layout (960×720, Chicago 12, white desk, 1px black rules):**

```
[ Repl                          File  Edit          ]
┌───────────────────────────────────────────────────┐
│ transcript (scrollable, Chicago 12)               │
│ lux> 5                                            │
│   Stack: [5]                                      │
│ lux> 10 +                                         │
│   Stack: [15]                                     │
├───────────────────────────────────────────────────┤
│ Stack: [15]                                       │
├───────────────────────────────────────────────────┤
│ lux> █                                            │  ← input line
└───────────────────────────────────────────────────┘
```

- Menu bar via `UI::menubar` / `UI::menu` / `UI::item`.
  - **File:** Open Definitions… (`SF::`, write file contents to `/sys/lux/history`), Save Definitions… (read history, write via `/sys/file/…`), Save Transcript…, Restart (`MM_GRAPHICAL_CODE_BASE JMPSTACK`, same as `APP::esc-restart`).
  - **Edit:** Cut / Copy / Paste (input line ↔ `/sys/snarf`), Clear Stack, Clear Words.
- Transcript is append-only text in `TRANSCRIPT` plus a scroll offset cell. `UI::vscroll` for overflow. Do **not** reuse Quill’s editor, and do **not** park this buffer on the heap band.
- Input line is **app-local** in `INPUT_LINE` (not a new `UI::field` in v1). Printable insert, backspace, caret, left/right, home/end. Up/down walks `CMD_HIST`. Cmd/Ctrl-C/V uses snarf.
- Return: `VFS::write` the line to `/sys/lux/eval`, read the result into `EVAL_SCRATCH`, append `lux> <line>` plus result to the transcript, refresh the stack strip from `/sys/lux/stack` into `STACK_SCRATCH`, clear the input.
- `.s` / `words` can be typed as today, or reached from the menu; both hit the same device.
- `APP::on-kbd!` / `APP::on-mouse!` / `APP::on-frame!`. Esc still opens the system Continue / Restart / Quit overlay (`APP::modal!` while `SF::` is up).
- Window title `T"Repl"` via `APP::init`.

### 5. Docs

- `docs/using-lux.md`: delete the fictional host-side Cloister REPL. Document `./bin/cloister apps/Repl.lux` and the `/sys/lux` files.
- `docs/lux_tutorial.md`: add a one-line “in Cloister, open Repl” next to `luxrepl`.
- `README.md` Tools section: mention the app next to `luxrepl`.
- `docs/ui.md`: one sentence that Repl is the interactive Lux surface.
- `docs/memory-map.md` + `memory_map.h` comment: Repl occupancy as above.

## Key decisions

1. **Guest app, not host REPL.** Cloister remains the machine; Repl is a ROM. Matches `AGENTS.md` and the picker. The old `using-lux.md` story is documentation drift.
2. **Headless scratch VM, not in-image eval.** Forced by execute-only images and “no new opcodes.” Eval cannot paint or open Cloister devices. That is `luxrepl`’s contract, which the tutorial already teaches.
3. **Share `LuxSession` with `bin/luxrepl`.** One evaluator, two skins. Stops the GUI and the CLI from diverging on `@word` / stack replay.
4. **VFS files, not a new SCI command.** I/O is files. Mirrors `/sys/snarf` and `/sys/vm/…`. Guest code stays ordinary `VFS::open/read/write`.
5. **No `UI::field` in v1.** One line editor in the app. Extract to the toolkit only if a second caller appears.
6. **Cycle cap on eval.** A GUI frame must return. Share the cap with `luxrepl` so a runaway word is consistent.
7. **`exit` in the GUI HALTs the ROM** (picker returns). Esc is still the system menu.
8. **Use existing `memory_map.h` bands, do not add a Repl band.** Cells at `0x880000` (`MM_APP_SMALL_STATE`). Buffers at `0x900000` (`MM_APP_BULK_BUFFER`). Eval at `MM_HEADLESS_CODE_BASE`. Heap at `0xA00000` is off-limits. C files include the header; Lux files comment the macro names.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Restore a REPL inside `cloister.c` (what `using-lux.md` describes) | Bypasses the app framework, duplicates UI in C, fights “Cloister is the machine.” |
| Relax image protection / add an executable eval buffer | New VM memory rule, adjacent to new opcodes. Eval could smash the Repl UI. |
| Spawn eval via `/sys/vm/new` | That’s a child **graphical** machine. Policy: no rio. Wrong lifetime and framebuffer. |
| Self-hosted Lux interpreter in Lux | Huge, incomplete, slow. The C compiler already exists. |
| Keep eval logic only in the guest by shipping bytecode blobs | Guest still cannot execute them. |
| Text protocol for `/sys/lux/stack` | Parsing integers in Lux is painful (`lib/str.lux`). Binary `LOADI` is the house style (ARCHITECTURE.md: binary events). |
| Park transcript at `0xA00000` (previous plan) | That is `MM_LUX_HEAP_BASE`. Same class of collision the map was written to stop (`docs/memory-map.md`, Quill `PASTE_BUF`). |
| New `MM_REPL_*` band | The app-small-state and app-bulk-buffer comments exist for this. A dedicated band is reserved for a new *category* of data, not one ROM. |
| Put the 128 KB transcript in `MM_APP_SMALL_STATE` | Header: “Large buffers do NOT belong here.” |

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| `compile_source` error paths miss the new buffer | Major | Central `compiler_errorf`; tests for each existing stderr string. |
| Replaying stack as decimal literals diverges from in-VM values | Minor | Same as `luxrepl` today; keep it. |
| 16KB host history fills | Minor | Same cap as CLI; `ERROR history full`. Save/Open is the escape hatch. |
| Eval of `INCLUDE "lib/app.lux"` pulls graphical code into a headless VM | Minor | Document: REPL is for the language, not for APP/DRAW experiments. |
| Guest string handling bugs in the transcript | Major | Display `/sys/lux/eval`’s text blob; binary stack is only for the strip. |
| Cycle cap false-positives | Minor | 1e6 is far above tutorial snippets; `#define` in `lux_eval.h`. |
| Address collision with SF/UI/DRAW | Major | Use `0x880000` / `0x900000` layout above; `luxc` duplicate-const warning on `make apps`. |
| Transcript grows past 128 KB | Minor | Clamp and drop from the top; do not overflow into the heap band. |

## Rollout

1. Engine + compiler errors + `luxrepl` switchover (`MM_HEADLESS_CODE_BASE`). `make test` must pass; `luxrepl` session from the README still works.
2. VFS device + `test_vfs`.
3. `lib/lux.lux` + `apps/Repl.lux` on the map above. `make apps`. Manual: picker → Repl.lux → tutorial session (`5`, `10`, `+`, `@double dup + ;`, `21 double`).
4. Docs, including `docs/memory-map.md` / header comment.

Rollback: each PR is additive. The device is unused until the app ships. `luxrepl` refactor is behavior-preserving.

## Test plan

- `make test` (compiler error strings, vfs lux device, existing suites).
- `make apps` then `./bin/cloister apps/Repl.lux`. Confirm `luxc` does not warn about duplicate address constants in Repl’s unit.
- Walk `docs/lux_tutorial.md` Numbers / words / `.s` inside the app.
- File → Open a `.lux` of definitions, then call a word.
- Paste via snarf into the input line.
- Unknown word: transcript shows the error, stack strip unchanged.
- Esc overlay still works over the REPL.
- Recursive runaway word: UI stays alive, `LIMIT` in the transcript.
- Sanity: Repl cells stay in `0x880000`–`0x880200`; buffers stay in `0x900000`–`0xA00000`; no symbol at `0xA00000` except via `MEM::` if the UI heap is used for controls.

No browser. Closest substitute: `test_vfs` + `test_compiler` + a manual Cloister run.

## PR plan

### PR 1 — Shared `LuxSession` + compiler errors

- **Files:** `include/lux_eval.h`, `src/lux_eval.c`, `src/repl.c`, `include/compiler.h`, `src/compiler.c`, `src/test_compiler.c`, `Makefile`
- **Deps:** none
- Extract eval from `repl.c`. `#include "memory_map.h"`; load eval at `MM_HEADLESS_CODE_BASE`. Capture compiler errors. `luxrepl` uses the new API. Tests for error strings and a couple of eval cases (can live in `test_compiler` or a new `test_lux_eval`).

### PR 2 — `/sys/lux` VFS device

- **Files:** `src/vfs.c`, `include/system.h` (session pointer), `src/system.c` (free), `src/test_vfs.c`
- **Deps:** PR 1
- Implement the file table above. Tests listed in §3.

### PR 3 — `lib/lux.lux` + `apps/Repl.lux`

- **Files:** `lib/lux.lux`, `apps/Repl.lux`, `lib/vfs.lux` (optional path constants), `include/memory_map.h` (add Repl to the small-state comment)
- **Deps:** PR 2
- Guest bindings (no RAM globals) and the System 6 UI on the `0x880000` / `0x900000` layout. `make apps` produces `apps/Repl.bin`. Picker picks it up automatically (`apps/Picker.lux` lists `apps/*.lux`).

### PR 4 — Docs

- **Files:** `docs/using-lux.md`, `docs/lux_tutorial.md`, `docs/ui.md`, `docs/memory-map.md`, `README.md`
- **Deps:** PR 3
- Retract the host-side REPL story. Document the app, `/sys/lux`, and Repl’s occupancy in the memory map.

## Open questions

None that block implementation. Defaults:

- App name: `Repl.lux`.
- Eval is headless at `MM_HEADLESS_CODE_BASE`.
- `exit` in the GUI HALTs the ROM.
- Memory: `0x880000` cells, `0x900000` bulk, never the heap.

If those should change (live-image eval, rename, make Repl the Cloister default), say so before PR 1.
