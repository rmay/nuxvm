# Fit a Cloister app into 64 KB — Snake first

## Context

Cloister guests today get the whole 16 MB reserved map
(`nux_guest_memory_size()`, `include/vm.h:24-33`), and nothing in the
toolchain pushes back on size. `luxc` is a single-pass emit-as-you-parse
compiler (`src/compiler.c:1020` `compile_word_def`, driven from `:1229`)
with no reachability analysis, so every word in every transitively
`INCLUDE`d file lands in the image. That is why `apps/Hello.lux` — 873
bytes of source, four words of app code — produces a **63,612-byte** ROM,
while `examples/lux/hello.bin` (no UI library) is **72 bytes**. `RESERVE`
(`docs/reserve-directive.md`) has the same problem on the data side: it
bump-allocates every reservation in the compiled unit whether or not the
code that reads it survives.

The goal is a NUX machine whose **entire guest address space is 65,536
bytes** — code, literals, and all runtime state — running a real Cloister
app. `apps/Snake.lux` is the first target.

### The measured budget

Reachability over Snake's include closure (`lib/app.lux` → `lib/ui.lux`,
plus `lib/tilemap.lux`, `lib/draw.lux`, `lib/vfs.lux`, `lib/core.lux`,
`lib/str.lux`, `lib/event.lux`, `lib/time.lux`, `lib/geom.lux`,
`lib/mem.lux`):

| | now | after |
|---|---|---|
| Word definitions | 750 | 316 live (42%) |
| Word-body source | 84,965 B | 36,405 B live (43%) |
| `apps/Snake.bin` | **68,573 B** | ~25–30 KB (est. from the above) |
| `RESERVE` data | 317,060 B | **43,664 B live** |

The dead data is dominated by one entry: `MEM::HEAP` (262,144 B) — Snake
never allocates. The *live* data is dominated by two tunable buffers:
`UI::MB_XBUF` 16,384 (`lib/ui.lux:2498`) and `DRAW::BATCH_BUF` 16,384
(`lib/draw.lux:36`) — 75% of what survives. Dropping both to 4 KB puts
live data near 19 KB, so ~30 KB code + ~19 KB data ≈ **49 KB**, inside
65,536 with real headroom.

### Why the SCI trap has to move

The SCI trap band is `0x10000`–`0x11000` (`include/memory_map.h:31-32`),
i.e. it starts *at* 64 KB, and guest syscall registers sit at `0x100D0`–
`0x10124`. No machine can be 65,536 bytes while the trap lives there.
Page zero (`0x0`–`0x1000`) is unused — `MM_FX_GLOBALS_BASE` already
starts at `0x1000` — so the band relocates there and the whole map
telescopes underneath 64 KB.

This is a change to the VM contract, which `AGENTS.md` pins at **300K**.
`nux` must cool (e.g. to 299K). Apps stay at the everything-else default,
now **399K** after the palette cooldown; rule 5 is satisfied both before and
after (399K > 299K) and forces no cascade, but a compact app will not run on
a pre-cooldown VM — which is exactly what kelvin encodes. Note that cooling
`nux` *is* a foundation change, so it would force the 399K collective to
re-release on top of it.
**No opcodes are added.**

## Work

### W1 — Reachability DCE in `luxc` (code and reservations)

The whole floor is here; everything else is bookkeeping.

Do **not** reimplement name resolution. `resolve_word`
(`src/compiler.c:203-239`) has three tiers — exact case-insensitive, then
`current_module::name`, then `IMPORT` alias expansion — and duplicate
definitions resolve to the *earliest* dictionary entry. Reproducing that
in a separate scanner is where this goes wrong. Instead run the real
compiler twice over the same `TokenList`:

1. **Pass 1** compiles normally into a throwaway buffer. Add a
   `current_def` index to `Compiler`, set in `compile_word_def`, and have
   every successful `resolve_word` record an edge `current_def →
   resolved dictionary index`. Record `RESERVE` uses the same way (a
   reservation name resolves through the same path). Tokens compiled
   while `current_def < 0` — the top-level tail of `apps/Snake.lux`,
   `SNAKE::start` / `HALT` — are the BFS **seeds**. Quotation bodies are
   compiled inside their enclosing word, so `current_def` attributes them
   correctly even though they may be emitted out of line
   (`active_quot_idx`, `quot_saved_frames`).
2. **BFS** from the seeds over those edges. Mark the reachable word set
   and the reachable reservation set.
3. **Pass 2** recompiles from token 0 with the unreachable set in hand.
   `compile_word_def` returns early for a dropped word — consuming tokens
   through `;` without emitting bytecode and without `add_dict` — and the
   `RESERVE` handler skips allocation for a dead reservation. Pass 2's
   addresses are the authoritative ones.

Suppress the duplicate-address diagnostics (`record_addr_const`,
`warn_duplicate_addr_consts`, `warn_consts_inside_reservations`,
`src/compiler.c:113-201`) during pass 1 so they do not print twice, and
run them in pass 2 only — where they now correctly ignore dead code.

Forward references work unchanged: `UnresolvedRef` patching already
handles use-before-definition, and the BFS is over the whole token list,
so definition order is irrelevant.

Gate it behind `-gc` (on by default for `-target compact`, off otherwise)
so existing ROMs are bit-identical until asked otherwise.

### W2 — Move the SCI trap to page zero

Single-constant change plus the guest-side mirrors.

- `include/memory_map.h:30-32` — `MM_DEVICE_BASE 0x0`, `MM_DEVICE_END
  0x1000`. `DEVICE_MEMORY_OFFSET` / `DEVICE_MEMORY_SIZE`
  (`include/vm.h:15-16`) and the range check (`src/vm.c:185-186`) follow
  automatically; `SCI_PORT` and friends (`include/system.h:12-16`) are
  already offset-relative.
- `lib/vfs.lux:8-12` — five hardcoded constants (`0x100D0` … `0x10124`)
  become `0x00D0` … `0x0124`.
- `src/fluxio_codegen.c:23-27` — the same five as `FX_SCI_*`.
- `MM_HEADLESS_CODE_BASE` (`0x011000`) can stay where it is; headless
  layout is unaffected.

Note the one real regression: address 0 becomes a device address, so a
guest `LOAD 0` now reads the bus instead of returning RAM. Nothing in the
tree does that (checked), but it removes a would-be null-deref tell.
Update `docs/memory-map.md` and `ARCHITECTURE.md:99-106`, and cool `nux`
in `AGENTS.md`.

### W3 — A compact allocation policy in the compilers

Add `-target compact` to `luxc` (`src/luxc.c`) alongside
`graphical`/`headless`, and the equivalent to `fluxioc`. In compact mode
the compiler is the sole allocator and the layout is:

```
0x0000 .. 0x1000   SCI trap (W2)
0x1000 ..          program image (code + literals)
       ..          compiler-allocated data: RESERVE band / FX globals
                   bump-allocated from image_end upward
       <= 0x10000  hard ceiling
```

`MM_LUX_RESERVE_BASE` stops being a fixed `0xA00000` in this mode:
initialise `c->reserve_next` (`src/compiler.c`, `compiler_create`) to the
image end instead. Fluxio's `FX_GLOBALS_BASE` allocator gets the same
treatment, so code and globals cannot collide the way they would if both
claimed `0x1000`.

Overflow is a compile error, modelled exactly on the existing check at
`src/fluxio_codegen.c:1829-1833`: naming the band, the top, and the
ceiling.

Expose the final top as a `mem_top` out-parameter from
`compiler_compile` — that is what the host needs in W4.

### W4 — Host support for a 64 KB machine

Two gaps, both small.

1. **The framebuffer is not aliased below 2 MB.** `system_set_memory`
   (`src/system.c:379-390`) only points `screen_pixels` at guest RAM when
   `mem_size >= 0x200000`; otherwise `release_owned_screen_pixels`
   (`:356-362`) leaves it `NULL`, and the flip at `:656-657` needs *both*
   buffers, so a small machine draws nothing. The `screen_pixels_owned`
   flag (`include/system.h:53`) already exists but is never set true.
   Allocate a System-owned front buffer in `system_set_resolution`
   (`:392-407`) whenever `screen_pixels` is not an alias, and set the
   flag. `src/test_vfs.c:734-756` asserts `!screen_pixels_owned` today
   and will need updating for the new path.
2. **Plumb the memory size.** `machine_create`
   (`include/machine.h:12`) already takes `mem_size`; `src/cloister.c:322`
   passes `nux_guest_memory_size(...)`. Cloister compiles `.lux` in
   process (`src/cloister.c:296-311`), so it can pass W3's `mem_top`
   straight through — the source path, which is how `apps/Snake.lux` is
   launched, needs nothing else. For prebuilt `.bin` ROMs there is no
   header to carry the size (`src/luxc.c` ends in a bare `fwrite`); have
   compact builds emit a small `<name>.mem` sidecar and have Cloister
   read it when present, falling back to the full map. Do not add a ROM
   header — the flat-image property is load-bearing.

Snake's closure touches the framebuffer only through `/dev/draw` (no
reference to `0x100000` anywhere in it), so nothing else is needed for
this app.

### W5 — Make the big library buffers tunable

Four `RESERVE` sizes are hardcoded and account for nearly all remaining
data. Give each a `@`-constant that a build can override (or a
`-D`-style compiler define), defaulting to today's value so nothing else
changes:

- `lib/mem.lux:23` — `HEAP 262144` (dead for Snake; W1 drops it, but it
  should still be tunable for apps that do allocate)
- `lib/ui.lux:2498` — `MB_XBUF 16384`
- `lib/draw.lux:36` — `BATCH_BUF 16384`
- `lib/ui.lux:1788` — `LS_POOL 8192` (dead for Snake)
- `lib/draw.lux:12` — `BUF 4096`

For the compact Snake build, `MB_XBUF` and `BATCH_BUF` at 4 KB each is
the change that closes the budget.

## Verification

1. **Baseline, unchanged builds.** `make && make test` — the full suite
   (`src/test_compiler.c`, `src/test_fluxio_compiler.c`, `src/test_vm.c`,
   `src/test_vfs.c`, `src/test_abi_conformance.c`) must pass, and every
   existing `apps/*.bin` must be **byte-identical** to its pre-change
   build with `-gc` off. Capture `shasum` of all ROMs before starting.
2. **DCE correctness in isolation** (W1, before W2/W3 land). Build with
   `-gc` on at the normal `0x600000` base and confirm each app still runs
   in Cloister. Sizes should drop roughly to the fractions measured
   above; a word that vanishes and *is* needed shows up as a
   resolve failure at compile time, not a runtime fault, because pass 2
   drops it from the dictionary too.
3. **Trap relocation** (W2). `make test` again — `src/test_vfs.c` is the
   real exercise of the SCI path. Then run an unmodified app in Cloister
   and in headless `nux`.
4. **The 64 KB machine.** `luxc -target compact -o apps/Snake.bin
   apps/Snake.lux`; the reported `mem_top` must be `<= 0x10000`. Run
   `./bin/cloister apps/Snake.lux` and play a full game: menu bar, tile
   rendering, score text (`SNAKE::nbuf`), high-score persistence to
   `.snake_hi` via `/sys/file`, game-over overlay. A missing `RESERVE`
   that DCE wrongly pruned shows as corrupted state, so play past the
   first food.
5. **Headless probe for the tight paths.** Reuse the `machine_tick` probe
   pattern rather than eyeballing frames — drive Snake headless with a
   fixed input sequence and assert the score and body-ring state, so a
   pruning bug is caught deterministically.
6. **Report the number.** Final `apps/Snake.bin` size, final `mem_top`,
   and the split between code and data.

## Risks

- **Pass-1/pass-2 divergence (W1).** The two passes must see identical
  token streams and identical module/import state. Any resolution that
  depends on emission state would break the equivalence. `resolve_word`
  does not, but confirm nothing else does before relying on it.
- **Address 0 as a device address (W2).** Behaviour change for any guest
  that reads or writes address 0; nothing in the tree does, but it is a
  permanent contract change.
- **`quill_lux_*` and other shared test helpers.** Easel, Tabula and
  Whittle share them; if buffer-size constants in `lib/*.lux` change
  shape, enumerate the callers before any bulk edit.
- **Scope creep to other apps.** Snake is the target. Easel (145,202 B)
  and the Fluxio Quill path (1,126,474 B, 93.5% of it zero padding from
  `src/fluxlink.c:296-298`) are separate problems; the fluxlink padding
  in particular is worth fixing but is not on this path.
