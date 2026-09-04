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
 * Guest I/O is files under /dev and /sys — not a MMIO device ports. */
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

/* --- Legacy hand-picked app bands: MM_APP_SMALL_STATE_*, MM_APP_BULK_BUFFER_* ---
 * These two bands held every Lux app's and library's hand-picked globals --
 * window state and widget bookkeeping in the first, font blobs, file and
 * paste buffers, canvases and line caches in the second. Both are now
 * essentially empty: app and library state is compiler-allocated out of
 * MM_LUX_RESERVE_BASE below (docs/reserve-directive.md), which is what
 * ended the collisions docs/memory-map.md lists.
 *
 * One occupant is left, apps/Quill.lux's LINE_STARTS, which has no bound to
 * declare (worst case 4 bytes * FILE_BUF_MAX = 4MB) and so cannot be
 * reserved without first capping Quill's line count. It has the bulk band
 * to itself. Do not put anything new in either band -- use RESERVE. */
#define MM_APP_SMALL_STATE_BASE 0x800000
#define MM_APP_SMALL_STATE_END  0x900000
#define MM_APP_BULK_BUFFER_BASE 0x900000
#define MM_APP_BULK_BUFFER_END  0x0A00000

/* --- Compiler-managed Lux reservations: MM_LUX_RESERVE_BASE .. MM_LUX_RESERVE_END ---
 * Bump-allocated by src/compiler.c for the Lux `RESERVE <name> <bytes> ;`
 * directive, which replaces the hand-picked `@NAME 0xHEX ;` idiom for
 * ordinary app/library state (docs/reserve-directive.md). Owned by the
 * compiler alone -- nothing may hand-pick an address inside this band, and
 * luxc warns if a `@NAME 0xHEX ;` constant lands in a reserved span.
 * Deliberately placed in the gap between the Lux heap and the Fluxio bulk
 * band so that a partially-migrated tree can't collide: no RESERVE address
 * can ever equal an address some unmigrated app or library still picks by
 * hand.
 *
 * 3MB, which is enough for an app's bulk pages too (Easel's canvas and undo
 * page, Whittle's 768KB frame bank, Tabula's cell pool) -- only one app
 * occupies a VM at a time, so the budget is libraries plus one app. This
 * absorbed what used to be lib/mem.lux's separate 2MB heap band: that heap
 * is now itself a RESERVE'd block inside this band (lib/mem.lux), which is
 * why there is one allocator here instead of two adjacent ones. */
#define MM_LUX_RESERVE_BASE 0x0A00000
#define MM_LUX_RESERVE_END  0x0D00000

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
