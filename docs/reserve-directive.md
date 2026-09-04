# `RESERVE` — compiler-managed memory for Lux

## Why

Every piece of global state in a Lux program used to be a hand-picked
absolute address:

```forth
@CUR_VAL  0x8D0020 ;
```

There were 576 such declarations across `apps/` and `lib/` (101 in Easel
alone, 55 in `lib/ui.lux`), and the author was personally responsible for
keeping them from overlapping. That failed six times in ways serious enough
to write down — see "Collisions found and fixed" in
[`memory-map.md`](memory-map.md), including `apps/Hello.lux` storing its
text Y coordinate into `lib/ui.lux`'s control-list head on every frame.

The old guard (`warn_duplicate_addr_consts()` in `src/compiler.c`) can only
catch *exact* duplicate values, because a Lux constant carries no size. It
cannot see a constant sitting in the middle of someone else's buffer, which
is the more common shape of the bug.

`RESERVE` removes the class of bug instead of reporting it: you say how many
bytes you want, the compiler picks the address.

## Using it

```forth
MODULE CALC
RESERVE CUR_VAL 4 ;
RESERVE ACC_VAL 4 ;
RESERVE GRID  240 ;

@clear  0 CUR_VAL STOREI ;
```

A reserved name is a word that pushes an address — byte for byte what
`@CUR_VAL 0x8D0020 ;` compiles to (`PUSH addr; RET`). Every existing idiom
works unchanged: `LOADI`/`STOREI`, base + index arithmetic, `FIELDS`
offsets layered on top of a reserved base. Only where the number comes from
changes.

- The byte count is required and must be positive. Decimal or `0x` hex.
- Each reservation is word-aligned; a size that isn't a multiple of 4 is
  rounded up for the *next* reservation, so cells never straddle a word.
- Inside a `MODULE`, the name is qualified like any other word: `CALC::CUR_VAL`.
  A cell genuinely shared between two modules needs no special case — the
  second module names the symbol, the way `APP::modal-f` already aliases
  `UI::APP_MODAL` in `lib/app.lux`.
- The directive is top-level, recognised case-insensitively, like `FIELDS`.

## What it does not cover

**Buffers with no declarable bound.** You have to know how big the thing is.
That is usually a feature — stating `MAX_CELLS * CELL_SIZE` is better than
implying it by the distance to the next address — but `apps/Quill.lux`'s
`LINE_STARTS` genuinely has no cap (worst case 4 bytes × `FILE_BUF_MAX` =
4 MB, larger than the band), so it keeps a hand-picked address until Quill
caps its line count. Overflowing the band is a compile error naming the
band, not a silent wrap.

**Headless programs.** The band sits at 10 MB, inside the full guest map a
graphical machine gets (`nux_guest_memory_size()` in `include/vm.h`). A
headless `nux` machine is sized to just `[0, base + program)` — roughly
68 KB — so a headless program that reserves will fault on first access.
`RESERVE` is for Cloister apps, which is where the collisions were.

**Genuine contracts.** Addresses the C host or the VM agrees on stay
hand-picked and documented: the SCI registers (`lib/vfs.lux` vs
`include/system.h`), the framebuffer at `0x100000` (`src/system.c`),
`UI::MB_HOVER` (read by `src/cloister.c`), `MM_GRAPHICAL_CODE_BASE`. A
reservation address is chosen by the compiler and shifts when reservations
ahead of it change, so it is exactly the wrong tool for a fixed contract.

## Ordering and stability

Addresses are handed out in token order after include expansion, so they
are stable for a given source tree but shift when a `RESERVE` is inserted
ahead of others. That is fine for a self-contained program, and is the same
caveat `include/memory_map.h` already records for `fluxlink`'s trampoline
table. It does mean **you must not write a reservation address down
anywhere** — read it from the symbol dump instead.

## Finding a reserved address from outside

```bash
./bin/luxc -target graphical -symbols /tmp/calc.symtab.json apps/Calculator.lux
```

```json
{
  "symbols":      [ { "name": "CALC::CUR_VAL", "address": 6322351 } ],
  "reservations": [ { "name": "CALC::CUR_VAL", "address": 10798484, "size": 4 } ]
}
```

The two arrays are different things: `symbols` holds the *code* address of
the name's `PUSH/RET` stub, `reservations` holds the data address and size.
`fluxlink` reads only `symbols` and ignores the rest.

This is how host-driven tests locate app state. `src/test_compiler.c` used
to `#define` 34 app addresses (Tabula, Snake, Breakout, RoadEscape) and poke
guest memory through them in ~221 places; those now go through
`lux_reservation_addr()`, which reads this dump. Writing an address down in
C again would reintroduce exactly the duplication `RESERVE` removes — and it
would fail quietly, reading zeros rather than erroring.

## Migration status

Done, apart from the one buffer named above. Every library and every app has
its state allocated by the compiler:

| | |
|---|---|
| Migrated | all 22 files in `lib/`, all 15 apps in `apps/` |
| Band | `0xA00000`–`0xD00000` (3 MB) |
| Peak use | Quill, 1.4 MB (libraries ~50 KB + `lib/mem.lux`'s heap 256 KB + the app) |
| Headroom | 1.6–2.7 MB free depending on the app |

Only one app occupies a VM at a time, so the budget is "libraries plus one
app", which is why 3 MB is comfortable.

Three hand-picked addresses remain, all deliberately:

- `lib/vfs.lux`'s SCI registers (`0x100D0`+) — a contract with
  `include/system.h`, which writes the same numbers.
- `lib/ui.lux`'s `MB_HOVER` (`0x8E0F60`) — `src/cloister.c` reads this exact
  address to line its menu-hover log up with the guest's, behind
  `NUXVM_MENU_DEBUG=1`.
- `apps/Quill.lux`'s `LINE_STARTS` — no declarable bound, see above.

A reservation address is chosen by the compiler and shifts when reservations
ahead of it change, so it is exactly the wrong tool for the first two.

### What the migration turned up

Stating sizes, rather than implying them by the gap to the next address,
found things the exact-duplicate check never could:

- **A live bug in `lib/sf.lux`.** The breadcrumb dropdown keeps up to 16
  path-component offsets at `dd-off`; `picked` and `cancld` were hand-placed
  4 and 8 bytes into that array. Walking a path two components deep wrote
  component offsets straight through the "user picked a file" and "user
  cancelled" flags — `SF::picked?` returned 15 after merely opening a
  folder. Regression test:
  `test_sf_dropdown_does_not_clobber_result_flags` in `src/test_compiler.c`.
- **A latent overflow in `lib/ui.lux`.** `MB_XBUF`, the `/dev/menu` export
  buffer, had 8192 bytes of room against a 14490-byte worst case. It is now
  reserved at 16384.
- **~78 KB of dead state in `lib/memory.lux`** — 24 constants for a dialog
  and file-dialog system nothing referenced. Dropped rather than reserved.
- **`lib/draw.lux` borrowing another module's address by literal.** A bare
  `0x530900` in `draw-int`, twice, was a silent copy of
  `MEMORY::DIALOG_SCRATCH_BUF`. It has its own `INT_BUF` now.
- **An unused `PATH_BUF`** in `apps/Quill.lux`.

Tests that used to hard-code app addresses in C — 34 `#define`s across the
Snake, Breakout, RoadEscape and Tabula suites — now resolve them through
`lux_reservation_addr()`, so the address exists in exactly one place.

## Implementation

- `include/memory_map.h` — the band.
- `src/compiler.c` — `compile_reserve()` / `skip_reserve()`, dispatched from
  both passes of `compiler_compile()` (pass 1 allocates, pass 2 skips);
  `record_reservation()`; `warn_consts_inside_reservations()`.
- `include/compiler.h` — the bump pointer and the reservation table.
- `src/luxc.c` — `write_symtab()`'s `reservations` array.
- `src/test_compiler.c` — `test_reserve_directive()`,
  `test_reserve_errors()`, `test_reserve_overlap_warning()`, and
  `test_calculator_reserved_state()`, which drives the real app through
  `7 * 6 =` to prove the relocation is transparent.
