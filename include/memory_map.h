#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

/* Single authoritative address-space partition for the NUX VM, shared by
 * luxc, fluxioc, fluxlink, and the VM itself. Every reserved band gets a
 * name here instead of being a hand-picked hex literal in some app or
 * library file -- that ad hoc practice already produced real collisions
 * (see docs/memory-map.md for the incident list). New bands must be added
 * here, not invented locally.
 *
 * Reserved address space ends at MM_TOTAL_MEMORY (16MB). Hosts size the
 * contiguous guest buffer with nux_guest_memory_size() in vm.h: graphical
 * machines get this full map; headless machines get [0, base+program).
 * Bands below are laid out with generous headroom so unrelated categories
 * of data are never adjacent enough to collide by a rounding error.
 */

/* --- Fluxio small-scalar globals: MM_FX_GLOBALS_BASE .. MM_FX_GLOBALS_END ---
 * Bump-allocated by fluxio_codegen.c for ordinary int/byte scalars and
 * small arrays. Below device space and below every program's code, so
 * writes never fault (self-modifying-code protection only covers the
 * loaded program image). ~60KB budget -- NOT for large buffers, see
 * MM_FX_BULK_GLOBALS_BASE below. */
#define MM_FX_GLOBALS_BASE   0x001000
#define MM_FX_GLOBALS_END    0x010000

/* --- SCI trap: MM_DEVICE_BASE .. MM_DEVICE_END ---
 * System-call registers for the Plan 9 VFS (SCI_PORT / CMD / ARGs).
 * Guest I/O is files under /dev and /sys — not Varvara MMIO device ports. */
#define MM_DEVICE_BASE        0x010000
#define MM_DEVICE_END          0x011000

/* --- Headless program code: starts at MM_HEADLESS_CODE_BASE ---
 * Where `nux` loads a compiled .bin (Lux or Fluxio, -target headless). */
#define MM_HEADLESS_CODE_BASE 0x011000

/* --- Shared small Lux flags/state: MM_SHARED_LUX_FLAGS_BASE .. MM_SHARED_LUX_FLAGS_END ---
 * A tiny pocket for small cross-cutting Lux library state that doesn't
 * belong to any one app (e.g. lib/log.lux's enabled flag). Kept separate
 * from lib/memory.lux's own dialog-state block (0x520000+) so the two
 * don't need manual coordination either. */
#define MM_SHARED_LUX_FLAGS_BASE 0x500000
#define MM_SHARED_LUX_FLAGS_END  0x510000

/* --- Graphical program code: starts at MM_GRAPHICAL_CODE_BASE ---
 * Where `cloister` loads a compiled .bin (Lux or Fluxio, -target graphical). */
#define MM_GRAPHICAL_CODE_BASE 0x600000

/* --- ABI library-link band: MM_ABI_LIBRARY_LINK_BASE .. MM_ABI_LIBRARY_LINK_END ---
 * Where `fluxlink` places a linked Lux-library trampoline stub + code/data,
 * so a Fluxio program's `extern` calls have a fixed target. See
 * docs/quill_fluxio.md Phase B3/Phase 0.5 for the trampoline/versioning
 * format. A Lux library build targets this band via `luxc -base`. */
#define MM_ABI_LIBRARY_LINK_BASE 0x700000
#define MM_ABI_LIBRARY_LINK_END  0x800000

/* The trampoline table (magic/version header + one 5-byte JMP per exported
 * symbol, see abi/nux-abi.json) lives in a FIXED-SIZE reserved region at
 * the front of the library-link band, not a region sized to the current
 * export count. This matters: a Lux library is compiled via `luxc -base`
 * to expect its own code at one fixed address -- if the trampoline table
 * directly preceded it and grew/shrank with each export added, the
 * library's code (and therefore every internal CALL/JMP address baked
 * into it at compile time) would silently shift on every recompile. A
 * fixed reserve means MM_ABI_LIBRARY_CODE_BASE never moves regardless of
 * how many symbols are exported (up to the cap below), so `luxc -base
 * MM_ABI_LIBRARY_CODE_BASE` stays correct across recompiles. 0x1000 fits a
 * 12-byte header plus 816 export slots (5 bytes each) -- comfortably more
 * than any real library (lib/ui.lux + lib/sf.lux together export a few
 * dozen at most). See src/fluxlink.c. */
#define MM_ABI_TRAMPOLINE_RESERVE 0x1000
#define MM_ABI_LIBRARY_CODE_BASE (MM_ABI_LIBRARY_LINK_BASE + MM_ABI_TRAMPOLINE_RESERVE)

/* --- App small-state band: MM_APP_SMALL_STATE_BASE .. MM_APP_SMALL_STATE_END ---
 * Hand-picked small globals for individual Lux apps/libraries (window
 * state, widget bookkeeping, etc: Snake, UIDemo, SF, lib/app.lux,
 * Calculator, lib/ui.lux, lib/cff.lux, lib/draw.lux, Illumos). Only one
 * app occupies a VM at a time today, so sub-collisions here between two
 * *different* apps are inert -- but nothing that expects to be linked in
 * (like lib/ui.lux, once Phase B lands) should assume it owns this whole
 * band. Large buffers do NOT belong here -- see MM_APP_BULK_BUFFER_BASE. */
#define MM_APP_SMALL_STATE_BASE 0x800000
#define MM_APP_SMALL_STATE_END  0x900000

/* --- App bulk-buffer band: MM_APP_BULK_BUFFER_BASE .. MM_APP_BULK_BUFFER_END ---
 * Large hand-authored Lux buffers that used to be scattered ad hoc across
 * 0x800000-0xA10000 (font glyph data, file buffers, paste buffers, line
 * caches, path scratch buffers). Consolidated here specifically so they
 * stop colliding with lib/mem.lux's heap (which used to start right where
 * a paste buffer was also parked -- see docs/memory-map.md). */
#define MM_APP_BULK_BUFFER_BASE 0x900000
#define MM_APP_BULK_BUFFER_END  0x0A00000

/* --- lib/mem.lux bump-allocator heap: MM_LUX_HEAP_BASE .. MM_LUX_HEAP_END ---
 * Exclusively owned by lib/mem.lux (both its metadata pointer and the
 * heap it manages) -- nothing else may place a global in this range. */
#define MM_LUX_HEAP_BASE 0x0A00000
#define MM_LUX_HEAP_END  0x0C00000

/* --- Fluxio bulk-array globals band: MM_FX_BULK_GLOBALS_BASE .. MM_FX_BULK_GLOBALS_END ---
 * Bump-allocated by fluxio_codegen.c for large global byte[]/int[] arrays
 * that don't fit the small-scalar budget (MM_FX_GLOBALS_BASE), e.g. a 1MB
 * text-editor file buffer. Kept well clear of every hand-authored Lux
 * band above so a Fluxio program linked against a Lux library (Phase B)
 * can't collide with it no matter how large its own buffers get. */
#define MM_FX_BULK_GLOBALS_BASE 0x0D00000
#define MM_FX_BULK_GLOBALS_END  0x1000000

/* Exclusive end of the reserved map -- the guest RAM size for a machine
 * that must be able to STORE into every named band (Cloister, child VMs). */
#define MM_TOTAL_MEMORY         MM_FX_BULK_GLOBALS_END

/* Everything above MM_TOTAL_MEMORY is unreserved -- available for future
 * bands. Add them here, with a comment explaining what they're for, rather
 * than picking an address locally. */

#endif /* MEMORY_MAP_H */
