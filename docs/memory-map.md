# NUX VM memory map

Single authoritative address-space partition, defined in `include/memory_map.h`
and included by `luxc`, `fluxioc`, `fluxlink` (once it exists), and the VM
itself (`include/vm.h`, `include/fluxio_codegen.h`). Every reserved band has
a name here — new bands get added to `memory_map.h` with a comment
explaining what they're for, not hand-picked locally in some app or library
file. That ad hoc practice is exactly what produced the collisions below.

**New state should not be hand-picked at all.** The Lux
`RESERVE <name> <bytes> ;` directive makes the compiler allocate the
address, which is what actually removes the collision class documented
below — the hand-picked bands and the incident list exist for the 576
declarations that predate it. See [`reserve-directive.md`](reserve-directive.md)
for what it does and does not cover (bulk buffers, headless programs and
genuine host/VM contracts still take a hand-picked address).

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
| Shared small Lux flags/state *(legacy)* | `0x500000`–`0x510000` | — | **Empty.** Held `lib/log.lux`'s enabled flag and `lib/time.lux`'s `/dev/time` scratch; both are now `RESERVE`d. |
| Graphical program code | starts at `0x600000` | `cloister` | Where a compiled `.bin` loads for `-target graphical` (both Lux and Fluxio). |
| ABI library-link band | `0x700000`–`0x800000` | `fluxlink` (planned, `docs/quill_fluxio.md` Phase B3) | Trampoline stub + linked Lux library code/data, so a Fluxio program's `extern` calls have a fixed target. A Lux "library build" targets this band via a `luxc -base` override. |
| App small-state band *(legacy)* | `0x800000`–`0x900000` | — | **Empty.** Held every app's and library's hand-picked small globals until they moved to `RESERVE`. Nothing new goes here. |
| App bulk-buffer band *(legacy)* | `0x900000`–`0xA00000` | `apps/Quill.lux` | Held the large hand-authored buffers (font blobs, file/paste buffers, canvases). All migrated to `RESERVE` except Quill's `LINE_STARTS`, which has no declarable bound — worst case 4 bytes × `FILE_BUF_MAX` = 4 MB — and now has the band to itself. |
| Compiler-managed Lux reservations | `0xA00000`–`0xD00000` | `src/compiler.c` exclusively | Bump-allocated by the Lux `RESERVE <name> <bytes> ;` directive — see [`reserve-directive.md`](reserve-directive.md). Holds all app and library state, including `lib/mem.lux`'s heap (itself a reservation, which is why the old separate 2 MB heap band is gone). **Nothing may hand-pick an address in this range**; `luxc` warns if a `@NAME 0xHEX ;` constant lands in a reserved span. |
| Fluxio bulk-array globals | `0xD00000`–`0x1000000` | `fluxio_codegen.c` (planned, `docs/quill_fluxio.md` Phase 0 deliverable 4) | Bump allocator for large global `byte[]`/`int[]` arrays that don't fit the small-scalar budget (e.g. a 1MB text-editor file buffer). |
| Unreserved | `0x1000000`+ (`MM_TOTAL_MEMORY` and above) | — | Available for future bands — add them to `memory_map.h`, not locally. |

## Collisions found and fixed

*Historical record.* These six were found by surveying hand-picked addresses
and fixed by moving them; the bands they refer to (the app small-state and
bulk buffer bands, `lib/mem.lux`'s old heap band) no longer hold live state.
They are kept because they are the evidence for why `RESERVE` exists — every
one of them is a bug that a compiler-chosen address makes unrepresentable.

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

`apps/Quill.lux`'s `LINE_STARTS` (now `0x900000`) is the last hand-picked
buffer in the tree. It is sized dynamically at runtime — up to 4 bytes per
line, worst case (every line blank) `FILE_BUF_MAX` lines, i.e. 4MB — so
there is no bound to hand `RESERVE`. Giving it one means capping Quill's
line count and making the indexer respect that cap: a change to how Quill
handles pathological files, not a change of address, and deliberately not
bundled into the migration.

It is better off than it was: every other Quill buffer moved into the
reservation band, so `LINE_STARTS` now has the whole former App Bulk-Buffer
band to itself, and `lib/mem.lux`'s heap — which an overrun used to run
into — is no longer adjacent to it.

## Collision checking

Lux now has a compiler-managed allocator — `RESERVE` (see
[`reserve-directive.md`](reserve-directive.md)) — and state declared through
it cannot collide at all. The checks below cover what is still hand-authored:
the 576 pre-existing constants, plus bulk buffers and host/VM contracts,
which `RESERVE` deliberately doesn't take over.

`luxc` runs a best-effort compile-time check (`record_addr_const`/
`warn_duplicate_addr_consts` in `src/compiler.c`): every `@NAME 0xHEX ;`
constant across the whole compiled unit (including transitively-included
files), except ones named like a color constant (`CLR`/`COLOR`, the
codebase's dominant source of *intentional* duplicate hex values), gets
recorded, and any value shared by more than one name prints a warning to
stderr. This is a warning, not a hard error — sub-range overlap (one
buffer's span containing another's base address, not just an exact
duplicate, like the original `log.lux`/`FILE_BUF` case) isn't caught, since
Lux constants carry no size information to check against.

A second check (`warn_consts_inside_reservations`) does catch containment,
in the one place sizes exist: a hand-picked `@NAME 0xHEX ;` landing inside a
span handed out by `RESERVE`. Names ending in `END` are skipped — those are
exclusive upper bounds (`MEM::HEAP_END` is literally the reservation band's
base address), not cells. This is what keeps a half-migrated file honest;
once a file uses `RESERVE` throughout, there is nothing left for it to find.
