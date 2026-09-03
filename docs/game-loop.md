# A real game loop: fixed 60 Hz simulation + a host key-up fix

> **Implemented.** This is the plan as approved. The shipped reference docs are
> `docs/games/breakout_clone.md` §5 and §7 (the key-up fix and the tick model)
> and `docs/lux_tutorial.md` ("Simulation vs. drawing"). Two things changed
> during implementation, both noted inline below:
> `system_freeze_monotonic_ms` had to be added so the headless harness could
> drive the new wall-clock simulation at all, and the `/dev/time` fd is opened
> in `APP::open-devices` rather than lazily in `on-tick!`. A follow-up added
> `APP::tick-alpha` / `APP::lerp` and moved Breakout's drawing onto them: a
> fixed step does not divide a frame period, so painting raw simulation state
> lurched on the frames that ran two steps. See `docs/games/breakout_clone.md`
> §7, "Interpolated drawing".

## Context

Breakout looks broken in the real app: you move the paddle once and it never
moves again. The cause is not the game loop — `apps/Breakout.lux` already uses
the standard framework (`APP::on-frame!` + `APP::loop`, `lib/app.lux:269`),
and `test_breakout_paddle_hold` in `src/test_compiler.c` already proves the
held-key logic works headlessly.

**Root cause: the SDL host never delivers KEY_UP to the guest.**
`src/cloister.c:512` correctly calls `queue_event(machine, 1, code, 0)` on
`SDL_KEYUP`, but `queue_event` (`src/cloister.c:172-187`) only forwards
type 0 to `system_push_kbd_event`:

```c
if (type == 0)             { system_push_kbd_event(...); }
else if (type >= 2 && type <= 4) { system_push_mouse_event(...); }
/* type 1 (KEY_UP) falls through both branches and is dropped */
```

So `@key-set` (`apps/Breakout.lux:450`) sets `kleft`/`kright` on key-down and
**never clears them**. Press Left, then Right: both flags are stuck on,
`@move-paddle` (`apps/Breakout.lux:358`) does `px - 6` then `px + 6`, and the
paddle is frozen for the rest of the session. That is the reported symptom
exactly. The headless test misses it because `breakout_key`
(`src/test_compiler.c:3191`) writes packets straight into the kbd channel,
bypassing `cloister.c` entirely.

Two design gaps came out of the same investigation and are worth closing now:

1. **Simulation is coupled to render.** One `on-frame` call does physics *and*
   a full repaint. The host paces to 60 fps with `SDL_Delay`
   (`FRAME_TARGET_MS`, `src/cloister.c:476`, vsync off by default), but a slow
   frame simply makes the *game* slower — there is no catch-up. Snake has the
   same issue; its `speed` is measured in render frames.
2. **The framework reads one keyboard packet per frame** (`lib/app.lux:293`
   uses `?`, not the `|:` drain the mouse gets at `:281`). With key-down and
   key-up pairs at 60 Hz this backs up; `apps/fluxio/Quill.fx:1544` carries a
   comment about exactly this costing ~1s of input lag.

Outcome: games get a real fixed-timestep loop — simulation on a 16 ms clock
driven by `/dev/time`'s monotonic milliseconds, painting once per rendered
frame — and held keys actually work under `bin/cloister`.

## 1. Host: deliver KEY_UP (`src/cloister.c`, `src/system.c`, `include/system.h`)

The mapping from a host event to the System queues is currently trapped inside
static `queue_event` in `cloister.c`, which no test binary links (see the
`Makefile:158-182` test targets — none include `cloister.o`). Extract the pure
part so it is testable:

- Add to `src/system.c`, declared in `include/system.h` next to
  `system_push_kbd_event` (`src/system.c:647`):

  ```c
  void system_push_host_event(System* sys, uint32_t type, uint32_t data, uint32_t mods);
  ```

  It contains the body of `queue_event`'s dispatch: the `events[]` ring write
  plus the kbd/mouse fan-out — with the fix that **types 0 and 1 both** go to
  `system_push_kbd_event` (passing `type` through, which
  `system_push_kbd_event` already stores verbatim as the packet type).
- `queue_event` in `src/cloister.c:172` becomes a thin forwarder to it. No SDL
  types cross the boundary, so `system.c` stays SDL-free.

## 2. Framework: fixed-timestep tick (`lib/app.lux`)

### New state

Append after `@v-font 0x8C0088` (`lib/app.lux:33`) — confirm each address is
free against `docs/memory-map.md` and against `UI::APP_MODAL`, which is aliased
into this band at `lib/app.lux:35`:

```
@v-tick   0x8C008C ;  ( simulation hook, 0 = not registered )
@tick-acc 0x8C0090 ;  ( unspent milliseconds )
@tick-ms  0x8C0094 ;  ( monotonic ms at the previous frame )
@tfd      0x8C0098 ;  ( persistent /dev/time fd )
@tbuf     0x8C00A0 ;  ( 16-byte /dev/time snapshot )
@STEP_MS  16 ;        ( ~60 Hz simulation step )
@MAX_STEPS 5 ;        ( catch-up cap; beyond this, drop the backlog )
@MAX_DT   100 ;       ( never replay more than 100ms of a stall )
```

`lib/time.lux`'s `TIME::milli@` re-opens and closes `/dev/time` on every call
(`lib/time.lux:22-30`) — one open+read+close per frame is wasteful in a 60 Hz
loop, so `APP` keeps its own `tfd` open. Reuse `TIME::load-int-le`
(`lib/time.lux:14`) for the LE decode rather than rewriting it.

### New words

```
@on-tick! ( addr -- )   ( registers the sim hook; also opens tfd and
                          seeds tick-ms / zeroes tick-acc )
@now-ms   ( -- ms )     ( tfd read into tbuf, TIME::load-int-le at tbuf 12 + )
```

Open `/dev/time` in `@open-devices` (`lib/app.lux:180-211`) alongside the other
devices, following the same retry-on-failure pattern.

### Loop change (`lib/app.lux:269-339`)

Two edits inside the existing iteration, keeping `begin-frame` / `end-frame` /
`VFS::yield` boundaries exactly where they are:

1. **Drain the keyboard**, matching the mouse drain at `:281` — turn the
   single `kfd LOADI kpkt 8 VFS::read-noyield 8 = [ ... ] ?` at `:293` into a
   `[ kfd LOADI kpkt 8 VFS::read-noyield 8 = ] [ ... ] |:` loop. The ESC-menu
   intercept body moves inside the loop unchanged.
2. **Insert the tick phase** immediately before the existing frame hook at
   `:324-327`, gated on `v-tick` so every current app (Easel, Tabula, Whittle,
   Quill, UIDemo, Illumos, Nib, Calculator, Picker) is bit-for-bit unaffected
   and pays no `/dev/time` read:

   ```
   ( 3. Simulation — fixed 16ms steps, catch-up capped )
   v-tick LOADI 0 > [
       now-ms { now }
           now tick-ms LOADI - { dt }
               dt 0 < [ 0 dt! ] ?          ( first frame / clock jump )
               dt MAX_DT > [ MAX_DT dt! ] ? ( don't replay a long stall )
               now tick-ms STOREI
               tick-acc LOADI dt + tick-acc STOREI
           UNGIRD
       UNGIRD
       0 { steps }
           [ tick-acc LOADI STEP_MS >= steps MAX_STEPS < AND ] [
               v-tick LOADI CALLSTACK
               tick-acc LOADI STEP_MS - tick-acc STOREI
               steps 1 + steps!
           ] |:
           ( a capped-out frame drops its backlog rather than
             spiralling into permanent catch-up )
           steps MAX_STEPS >= [ 0 tick-acc STOREI ] ?
       UNGIRD
   ] ?

   ( 4. Frame — paint only )
   v-frame LOADI dup 0 > [ CALLSTACK ] [ drop ] ?:
   ```

   Renumber the existing step comments in the loop accordingly.

## 3. `apps/Breakout.lux`

- Split `@on-frame` (`:484`): everything inside the `APP::esc-open? 0 =` guard
  — `move-paddle`, `ball-to-paddle`, `advance`, the `left-n <= 0` level-clear
  block — moves to a new `@on-tick`. `@on-frame` becomes just `paint`.
- Register both in `@start` (`:501`): `[ on-tick ] APP::on-tick!` beside the
  existing `[ on-frame ] APP::on-frame!`.
- **Fix `@advance` (`:270`)**: `dx`/`dy` are computed once before the sub-step
  loop, but `step-x`/`step-y` negate `vx`/`vy` on a wall, brick or paddle
  bounce (`:235-244`, `:212`, `:258-266`). The remaining sub-steps of that
  frame then keep pushing with the pre-bounce delta, re-clamping and
  re-beeping. Recompute `vx LOADI n /` / `vy LOADI n /` inside the loop body
  instead of hoisting them.
- With the loop now a true 60 Hz clock, `BASE_SPEED 576` (2.25 px/step) keeps
  its documented meaning rather than meaning "per render frame".

## 4. `apps/Snake.lux`

`@on-frame` (`:305`) mixes the `tick`/`speed` frame counter with the repaint.
Move the counter and `step` into a new `@on-tick`; leave the `fill-rect` /
`paint-bar` / `paint-field` / banner block in `@on-frame`. `speed` is a count
of steps and its meaning is unchanged (frames were already ~60 Hz), so no
retuning. Register `[ on-tick ] APP::on-tick!` in `@start` (`:342`).

## 5. Docs

- `docs/games/breakout_clone.md` — replace the "one keyboard packet per frame"
  framework note with the drain, and document the tick/paint split. Also fix
  the stale claim that positions are recovered with `8 ARSHIFT`; the code uses
  `FP /` (`apps/Breakout.lux:92-93`).
- `docs/memory-map.md` — register the new `0x8C008C-0x8C00AF` `lib/app.lux`
  cells in the app small-state band row (the file already has an uncommitted
  edit adding Breakout's `0x880000` block).
- `docs/ui.md` (or wherever `APP::on-frame!` is documented) — document
  `APP::on-tick!`, the 16 ms step, the 5-step catch-up cap, and the rule that
  simulation goes in `on-tick` and drawing in `on-frame`.
- Mirror this plan to `docs/game-loop.md` as the first implementation step.

## Verification

1. **`make test`** — the full suite must pass unchanged.
2. **New host-wiring test** in `src/test_vfs.c`, alongside `test_kbd_vfs`
   (`:~100`): call `system_push_host_event(sys, 1, keycode, 0)` and assert an
   8-byte `/dev/kbd` read returns a packet with `buf[0] == 1` and the right
   keycode. This is the regression that would have caught the original bug;
   without the `system_push_host_event` extraction there is no seam to test.
3. **Extend `test_breakout_paddle_hold`** (`src/test_compiler.c:3207`) past
   `S_SERVE`, which is as far as it currently gets. After serving: pump ~120
   ticks and assert `BREAKOUT_BX` actually changed and `state == S_PLAY`;
   assert the brick count at `0x880034` (`left-n`) drops below 60 once the
   ball reaches the wall; assert the ball's `by` reverses sign of travel after
   hitting the top edge. Note `quill_lux_pump` drives `machine_tick` faster
   than wall clock, so the test must either accept that `/dev/time` advances
   in real ms (pump enough iterations) or assert on ordering rather than an
   exact step count.
4. **`make apps`** — `Breakout.bin` and `Snake.bin` rebuild against the new
   `lib/app.lux`; every other app must still compile untouched.
5. **Manual, the actual bug**: `./bin/cloister apps/Breakout.bin`. Press Left,
   release, press Right — the paddle must move both ways repeatedly. Before
   this change the second direction freezes it permanently. Then serve and
   play a round: the ball bounces, bricks clear, lives decrement.
6. **Speed independence**: run under `NUXVM_FRAME_DEBUG=1` and confirm the
   game plays at the same speed with `NUXVM_VSYNC=1` set and unset — the whole
   point of the accumulator.
7. `./bin/cloister apps/Snake.bin` — one round, confirm movement speed is
   unchanged from before the refactor.
