# NUX VM memory map

Single authoritative address-space partition, defined in `include/memory_map.h`
and included by `luxc`, `fluxioc`, `fluxlink` (once it exists), and the VM
itself (`include/vm.h`, `include/fluxio_codegen.h`). Every reserved band has
a name here — new bands get added to `memory_map.h` with a comment
explaining what they're for, not hand-picked locally in some app or library
file. That ad hoc practice is exactly what produced the collisions below.

Reserved address space ends at `MM_TOTAL_MEMORY` (16 MB,
`MM_FX_BULK_GLOBALS_END`). Hosts size the contiguous guest buffer with
`nux_guest_memory_size()` in `include/vm.h`: graphical machines (Cloister,
child VMs) get this full map; headless `nux` / `luxrepl` get
`[0, base + program)` — about 68 KB for `examples/lux/hello.bin`.

| Band | Range | Owner | Purpose |
|---|---|---|---|
| Fluxio small-scalar globals | `0x001000`–`0x010000` | `fluxio_codegen.c` (`FX_GLOBALS_BASE`/`FX_DEVICE_BOUNDARY`) | Bump-allocated ordinary `int`/`byte` scalars and small arrays for Fluxio programs. ~60KB budget — not for large buffers. |
| SCI trap | `0x010000`–`0x011000` | `include/system.h` | VFS syscall registers (`SCI_PORT` / CMD / ARGs). Not device ports. |
| Headless program code | starts at `0x011000` | `nux` | Where a compiled `.bin` loads for `-target headless`. |
| Shared small Lux flags/state | `0x500000`–`0x510000` | small cross-cutting Lux library state | `lib/log.lux` at `0x500000`; `lib/time.lux` 16-byte `/dev/time` scratch at `0x500100`. Kept separate from `lib/memory.lux`'s dialog-state block (`0x520000`+). |
| Graphical program code | starts at `0x600000` | `cloister` | Where a compiled `.bin` loads for `-target graphical` (both Lux and Fluxio). |
| ABI library-link band | `0x700000`–`0x800000` | `fluxlink` (planned, `docs/quill_fluxio.md` Phase B3) | Trampoline stub + linked Lux library code/data, so a Fluxio program's `extern` calls have a fixed target. A Lux "library build" targets this band via a `luxc -base` override. |
| App small-state band | `0x800000`–`0x900000` | individual Lux apps/libraries | Hand-picked small globals (window state, widget bookkeeping): Snake, UIDemo, SF, `lib/app.lux`, Calculator, `lib/ui.lux`, `lib/cff.lux`, `lib/draw.lux`, Illumos, `lib/tilemap.lux` (`0x8D1000`–`0x8D1500`: layout/tileset-registry state), Whittle (`0x8A0000`–`0x8A03A0`: doc/UI state + name/path/header scratch buffers), `lib/ninep.lux` (`0x8D2000`–`0x8D2540`: 512-byte message build/parse buffer, a 16-entry named-file table, and an 8-entry fid table for the 9P2000 subset server; each caller-owned 16-byte connection block lives in the caller's own app-state, not here). Only one app occupies a VM at a time today, so collisions *between different apps* here are inert — but large buffers don't belong here. |
| App bulk-buffer band | `0x900000`–`0xA00000` | individual Lux apps | Large hand-authored buffers: font glyph data, paste buffers, line caches, path scratch buffers. Whittle's sprite editor reserves `0x900000`–`0x9C0000` (`FRAME_BANK`: 256 fixed 3072-byte RGB tile-pixel slots) and `0x9C0000`–`0x9C0600` (`SPRITE_TAB`: 16 fixed 96-byte sprite records, each holding a name, frame count/speed, and up to 16 frame-bank slot indices) — saved to disk as CSF (Cloister Sprite File, magic `"CSF1"`). Easel's 576×720 page is packed 1bpp via `lib/bitmap.lux` (`BITMAP::PAGE_BYTES` = 51,840): `CANVAS` `0x900000`, `UNDO` `0x90D000`, EAS2 save/load staging (`PACK`) `0x920000`, transform scratch (`TMP`) `0x934000`, `SEL_MASK` `0x991000` (lasso's filled-interior selection mask), 38 `PATTERNS` (8 bytes each) `0x990300`, 32 `BRUSHES` (16×16 1bpp masks, 32 bytes each) `0x990500`, the EASS clipboard payload (`SNARF_BUF`) `0x9A0000`, three CFF system fonts (`FONT_CHICAGO`/`FONT_GENEVA`/`FONT_MONACO`) from `0x9A8000`, and the floating text block's source bytes (`TEXT_BUF`) `0x9AE600`. |
| `lib/mem.lux` heap | `0xA00000`–`0xC00000` | `lib/mem.lux` exclusively | Bump-allocator heap + its own `HERE_ADDR` metadata (first 0x100 bytes). Nothing else may place a global in this range. |
| Fluxio bulk-array globals | `0xD00000`–`0x1000000` | `fluxio_codegen.c` (planned, `docs/quill_fluxio.md` Phase 0 deliverable 4) | Bump allocator for large global `byte[]`/`int[]` arrays that don't fit the small-scalar budget (e.g. a 1MB text-editor file buffer). |
| Unreserved | `0x1000000`+ (`MM_TOTAL_MEMORY` and above) | — | Available for future bands — add them to `memory_map.h`, not locally. |

## Collisions found and fixed

Surveying every hand-picked global address across `apps/*.lux`/`lib/*.lux`
turned up real, live collisions — harmless only because each compiled
program has so far been the sole occupant of its own VM instance:

1. **`lib/log.lux`'s `ENABLED-FLAG`** was `0x803000`, inside
   `apps/Quill.lux`'s `FILE_BUF` span (`0x800000`–`0x900000`,
   `FILE_BUF_MAX = 0x100000`). Moved to `0x500000` (shared Lux flags band).
2. **`lib/mem.lux`'s `HEAP_START`** was `0xA00000`, flush against
   `apps/Quill.lux`'s `PASTE_BUF` (also `0xA00000`). Fixed both ways:
   `lib/mem.lux` now reserves its own first `0x100` bytes for `HERE_ADDR`
   and starts the heap proper at `0xA00100`; `apps/Quill.lux`'s `PASTE_BUF`
   (and its `LINE_STARTS`, which was *also* inside the old heap band at
   `0xA10000`) moved into the App Bulk-Buffer band at `0x903000`/`0x913000`.
3. **`lib/mem.lux`'s `HERE_ADDR`** was `0x8E0000`, the same address
   `lib/ui.lux` uses as its own component-state base. Moved into
   `lib/mem.lux`'s own heap band (`0xA00000`, see above) — self-contained,
   no longer borrowed from another library's territory.
4. **`UI::MB_N` and `UI::MN_N`** both sat at `0x8E0F50`. `MB_N` was a
   leftover: written once in `UI::new` and never read. `MN_N` is the live
   menu count (how many titles are on the bar — per-menu item counts live
   in the `MN_COUNT` field of each menu record, not in a second global).
   Dropped `MB_N`; `UI::new` now zeros `MN_N` directly.
5. **`UI::APP_MODAL` and `APP::modal-f`** still share `0x8C004C` — that is
   intentional, the cross-module "is a modal dialog open" cell (Lux has no
   pointer-passing, so a shared cell is how they communicate). The
   duplicate-const warning was noise: `APP::modal-f` is now a word alias
   of `UI::APP_MODAL` rather than a second `@NAME 0xHEX ;` reservation.
6. **`apps/Hello.lux`'s `ty`** was `0x8E0010`, the same cell as
   `lib/ui.lux`'s `CS_HEAD` (and `tx` sat in the same UI block at
   `0x8E000C`). Hello includes `lib/app.lux` → `lib/ui.lux`, so every
   frame stored the text Y into the UI control-list head. Moved both cells
   to `0x8A0000` / `0x8A0004` with the other apps' small state.

These matter more once `apps/Quill.lux` is ported to Fluxio and linked
against a compiled `lib/ui.lux`/`lib/sf.lux` (`docs/quill_fluxio.md` Phase
B) — that work deliberately puts multiple independently-authored binaries
in one VM's address space at once, which is exactly when latent collisions
like these stop being inert.

## Known remaining gap

`apps/Quill.lux`'s `LINE_STARTS` (now `0x913000`) is sized dynamically at
runtime — up to 4 bytes per line, worst case (every line blank) up to
`FILE_BUF_MAX` lines — which can exceed the 1MB App Bulk-Buffer band and
spill into `lib/mem.lux`'s heap band. This is a pre-existing headroom
assumption, not introduced by the address moves above: `apps/Quill.lux`
never includes `lib/mem.lux`, so the overrun is harmless in practice, just
not formally bounded. Worth revisiting if `lib/mem.lux` and `Quill` are
ever linked into the same image (see Phase B).

## Collision checking

Lux still requires hand-authored addresses — there is no compiler-managed
allocator for it (a larger, separate change, out of scope for now). `luxc`
now runs a best-effort compile-time check (`record_addr_const`/
`warn_duplicate_addr_consts` in `src/compiler.c`): every `@NAME 0xHEX ;`
constant across the whole compiled unit (including transitively-included
files), except ones named like a color constant (`CLR`/`COLOR`, the
codebase's dominant source of *intentional* duplicate hex values), gets
recorded, and any value shared by more than one name prints a warning to
stderr. This is a warning, not a hard error — sub-range overlap (one
buffer's span containing another's base address, not just an exact
duplicate, like the original `log.lux`/`FILE_BUF` case) isn't caught, since
Lux constants carry no size information to check against.
