# Remove All Go — Finish the C Migration

## Context

nuxvm contains two parallel implementations with no cgo bridge: the original Go tree (`pkg/`, `cmd/`, 38 files, ~12.4k LOC, Ebiten GUI) and a newer pure-C tree (`src/`, `include/`, ~7.7k LOC, SDL2 GUI) that the Makefile already builds exclusively (`make` → bin/nux, bin/luxc, bin/luxrepl, bin/cloister + 3 test binaries). The repo's own `final_c.md` is the authoritative migration record; its remaining-work list was verified accurate against the code. The C port is ~80–85% done — VM, compiler/lexer, VFS/SCI, CFF font rendering all work. The goal is to close the remaining parity gaps, then delete every Go file in one atomic commit, leaving deps = C compiler + pkg-config + SDL2.

**User decision:** delete Go in this same run, after best-effort self-verification (automated tests + smoke-running the apps); no pause for manual human verification.

Per saved preference: **Step 0 of implementation is to mirror this plan into `docs/remove-all-go.md`** (couldn't be written during plan mode).

## Step 1 — Excise Go cross-validation dead code (XS)

`src/test_compiler.c`: delete `go_available()` (~line 1053), `compile_with_go_reference()`, `run_go_cross_case()`, `test_go_cross_validation()` — none called from `main()`. Also remove helpers used only by them (`states_match`/`snapshot_vm`/`VMState`, ~lines 1000–1051 — grep first) and now-unused includes. Verify: `make && ./bin/test_compiler`; `grep -rn "go run\|go_available" src/` → nothing.

## Step 2 — CLI parity (S)

- **`src/nux.c`** (ref `cmd/nux/main.go`): accept `.lux` — read as text, `compile_source(source, HEADLESS_BASE_ADDRESS, &len, false)` (`include/compiler.h:129`), exit 1 on NULL. Update usage.
- **`src/luxc.c`** (ref `cmd/luxc/main.go:15-163`): add `-trace` (4th arg of `compile_source`, currently hardcoded false at line 72), `-dumpAt <addr>` (dec or 0x-hex; dump and exit without writing .bin, matching Go), `-dumpRange <n>` (default 64). Port `dumpAround` (main.go:88-152): hex 16 bytes/line with `*` at target PC + best-effort disassembly via existing `opcode_name()` (`src/vm.c:7`); add `opcode_size()` (5 for PUSH/JMP/JZ/JNZ/CALL/LOAD/STORE, else 1); immediates print big-endian.
- **`src/repl.c`** (ref `pkg/luxrepl/repl.go:77-175`): add `help`/`?` (port `printHelp` verbatim), `words` (track defined word names; append to history *before* mutating the line), `history`, and Go's "definition must end with ';'" check.

Test: `./bin/nux apps/Hello.lux` matches `./bin/nux apps/Hello.bin`; `luxc -dumpAt` shows sane disassembly; plain compile produces byte-identical `.bin` vs pre-change; scripted REPL session.

## Step 3 — Cloister input parity (S–M)

`src/cloister.c` (ref `cmd/cloister/main.go:45-142, 261-300`). Verified: app-mode KEYDOWN (lines 301-309) only handles Escape — **no keys reach apps today**; branch at line 351 is unreachable.

- Add `translate_key(SDL_Keycode, bool shift, int32_t* out)`: arrows → 17/18/19/20 (Up/Down/Left/Right), PgUp/PgDn → 21/22, Tab 9, Return/KP_Enter 13, Esc 27, Backspace/Delete 8, Space 32, KP digits → '0'–'9'; a–z with shift → uppercase; digits with shift → `")!@#$%^&*("` indexed; symbol shift pairs (`-_ =+ [{ ]} \| ;: '" ,< .> /? ` + backtick→~); anything else (F-keys, bare modifiers) returns false → no event.
- `current_modifiers(SDL_Keymod)`: SHIFT→1, CTRL→2, ALT→4, GUI→8 (matches `pkg/system/services.go:102-105`, `lib/event.lux:31-34`).
- On app-mode KEYDOWN (accept `e.key.repeat` — replaces Go's repeat logic): push translated code to legacy event ring (`queue_event`, extending it to carry mods instead of hardcoded 0) AND `/sys/kbd` packet queue (layout already matches Go: `src/vfs.c:355-377`), then fire controller vector 4. **Remove the Escape hijack** — Go delivers 27 to the app (Snake's own pause menu depends on it); return-to-launcher already happens when `machine_tick` reports halted (line 376-379). Delete the unreachable 351-371 branch. KeyUps: Go never queues them to kbd; keep legacy ring only.
- Window title: `machine->system->set_window_title = cloister_set_title; title_ctx = win;` at both machine-creation sites (lines ~220 and ~289); `SCI_SET_WINDOW_TITLE` (`src/system.c:122-130`) already calls it. Reset title to "Cloister" on return to launcher.

Test: Snake steers with arrows; Quill typing incl. shifted symbols; title changes.

## Step 4 — File dialog modal (M)

New `src/dialog.c` + `include/dialog.h`; wire in `src/cloister.c`; add `dialog.o` to Makefile. Ref: `pkg/system/dialog.go` (port nearly line-for-line), consumer `apps/Quill.lux:657-700`. C plumbing exists: `dialog_write` (`src/vfs.c:438-446`) calls `sys->open_file_dialog`; result returned via `system_set_dialog_result` (`src/system.c:634-639`).

- `FileDialog` struct: active flag, current relative path (starts `"."`), malloc'd entry array (name + is_dir), selected/scroll, double-click tracking via `SDL_GetTicks()` (500ms).
- Keep Go geometry exactly (dialog.go:180-401): 400×300 centered, list 330×210 at +20/+40, itemH 20, scrollbar 20px with arrow boxes, Open/Cancel buttons.
- Listing (`refreshFiles`, dialog.go:98-153): `opendir` under `sys->sandbox_root` (reuse `resolve_host_path` pattern, `src/vfs.c:46-55`); prepend `..` unless at root; qsort priority `.. < hidden dir < dir < hidden file < file`, then strcasecmp. Return **relative** path (app reopens via `/sys/file/<path>`). Cancel → result `"cancel"` (Quill string-compares).
- Port the five icon bitmaps (dialog.go:29-86) verbatim; draw with static fillRect/drawRect/drawBitmap helpers writing `[0xFF,R,G,B]` **directly into `sys->screen_pixels`** after `machine_tick`, before the texture blit (drawing to back_pixels would be overwritten). Text via a refactored `system_draw_char` taking an explicit target buffer (Chicago at scale 1).
- Cloister loop: while dialog active, route translated KEYDOWN/mouse/wheel to `dialog_*` and do NOT queue to the VM (Go consumes events during modal); keep ticking the machine (Quill polls in a read/yield loop).

Test: Quill Open → modal, dir navigation, scroll, double-click load, Esc→"cancel", save/load round-trip. Also makes `SCI_OPEN_FILE_DIALOG` (cmd 21) work.

## Step 5 — Per-System VFS refactor + child VMs + ns binds (L, riskiest)

Refs: `pkg/system/vfs.go` (esp. 21-36, 59-132, 146-166, 374-480), `pkg/system/machine.go:92-153`, consumer `apps/Shell.lux:456-508`. Verified: `src/vfs.c:19-30` holds global statics (`fd_table`, `mount_table`, `mount_count`, `g_play_sound`, `g_last_chan_peer`).

- **5a State**: new `VFSState` (fd_table[1024], mounts[32] + count, last_chan_peer) defined in `include/vfs.h` (forward-declare System; vfs.h must NOT include system.h); embed `VFSState vfs;` in `struct System` (`include/system.h`). Zeroed state = valid empty, so calloc'd Systems work. Delete the statics, `vfs_init`, `vfs_set_sound_handler` (+ call at `cloister.c:180`).
- **5b Signatures**: all entry points gain leading `System*`: `vfs_open/read/write/close/seek/stat/bind`, plus new `vfs_state_free(System*)` called from `system_free` before freeing `child_vms`. Mechanical call-site updates in `src/system.c` `handle_sci` (lines 149-216).
- **5c Refcounts** (fixes latent double-free): `VFSFile` gains `refcount`. Open of a mounted path returns the **same** `VFSFile*` with refcount++ (delete the shallow clone at `vfs.c:805-809` — it shares private_data and double-frees; Go hands out the same object). Bind → refcount++. Close → `fd_table[fd]=NULL; if(--rc==0) f->close(f)`. Channels get a shared block freed when both endpoints die (also fixes current ChanPair leak). Deliberate deviation: a mount keeps its endpoint alive after Shell closes peer fds (Shell.lux:503-507) — strictly more correct than Go's race, same observable behavior.
- **5d ns binds**: in `vfs_bind`, parse `/sys/vm/<id>/ns/<rest>` → validate `sys->child_vms[id]` → bind onto the **child's** System at `/<rest>` (e.g. `/dev/draw`). Mount lookup already precedes the `/dev/→/sys/` remap (`vfs.c:803` vs 871), so child opens hit bound channels exactly like Go.
- **5e Audio**: `create_audio_file` carries its System; `audio_write` calls `sys->play_sound`. In `vm_write`, copy parent's `sandbox_root` and `play_sound` into the child.
- **5f Child ticking**: restructure `machine_tick` (`src/machine.c`) per Go `machine.go:94-153` — run parent slice (skip if halted), then always tick every `child_vms[i]` recursively, return `!halted`.
- **5g Tests**: migrate `src/test_vfs.c` to new signatures (NULL-sys call sites now build a System). Add regressions: bind/mount aliasing round-trip; Shell channel lifecycle (bind peer, close peer fd, data still flows through mount); child VM spawn + ns bind + parent tick ticks child; namespace isolation (parent `/dev/draw` ≠ child's bound file). One ASan build+run of `test_vfs` and a brief Shell session.

Test gate: `./bin/cloister apps/Shell.bin` — child app launches, drawing proxied, input forwarded through bound channels.

## Step 6 — Docs (S)

- `README.md`: remove "Why Go?", Go prereqs/build; replace with cc + pkg-config + SDL2, `make` / `make test`; fix project-layout tree; grep-scrub remaining go references.
- `examples/README.md`, `docs/*`, `ARCHITECTURE.md`: sweep `grep -rn "go run\|go build\|main\.go\|pkg/"` and fix.
- Mark `final_c.md` complete; note in `FULL_C_MIGRATION.md` that it's historical.

## Step 7 — Verification (best-effort, per user decision)

1. `make clean && make && make test` — all green, minimal warnings.
2. Purity: `grep -rn "go run\|go build\|golang" src/ include/ Makefile` → nothing (the `pkg_system_chicago12x12_cff` symbol in `src/chicago.h` is a C identifier, fine).
3. CLI checks: `nux` on `.lux`, `luxc -trace/-dumpAt`, scripted REPL (`help/words/history`).
4. Recompile all `apps/*.lux` with the C `luxc`; run results.
5. App smoke via `./bin/cloister` (launcher, Snake, Quill incl. dialog, Shell child VMs, Calculator/Hello) — launched and exercised as far as scriptable/observable; ASan pass on `test_vfs`.
6. Build succeeds without invoking `go` anywhere.

## Step 8 — Atomic deletion commit

```sh
git rm -r pkg cmd
git rm go.mod go.sum examples/examples.go
rm -rf pkg cmd                        # sweeps untracked "pkg/system/chicago12x12.cff copy"
find . -name '*.go' -not -path './.git/*'   # must print nothing
make clean && make && make test
git commit                            # single commit, nothing else in it
```

Keep: `lib/*.lux`, `apps/`, `resources/`, `examples/*.bin` + `examples/modules`, `src/chicago.h`, docs. Note: root-level `trace.go`/`diff.go` named in FULL_C_MIGRATION.md don't exist — nothing to do.

## Effort

XS + S + S–M + M + L + S ≈ 4–6 days of work; Step 5 (VFS refactor) is the risk center, mitigated by refcounts, new test_vfs regressions, and ASan.
