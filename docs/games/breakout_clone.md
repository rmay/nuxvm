# Breakout — design notes

`apps/Breakout.lux`. Arrow keys move the paddle, three lives, a score, and
levels that keep coming until you run out of lives.

```
./bin/cloister apps/Breakout.bin
```

## Context

Snake was the only game in the repo, and it moves on a grid: one cell per N
steps, no motion between cells. Breakout can't do that. The ball travels at a
fraction of a pixel per step and has to bounce at arbitrary angles off the
paddle, so this is the first Cloister app that needs **fixed-point arithmetic**
and **sub-step collision stepping** in the guest.

Everything else here is Snake's structure reapplied: fixed-address globals,
`UI::overlay-button` for the between-round buttons, a high score in
`/sys/file`, and beeps on `/dev/audio`. It splits simulation from drawing
across `APP::on-tick!` and `APP::on-frame!` — see §7 — and Snake has since been
moved onto the same pair.

`lib/tilemap.lux` is deliberately **not** used. It suits Snake because Snake's
cells are square — `TILEMAP::layout` derives a single `tile-px` for both axes.
Bricks are wide rectangles (48×16), so the brick grid is a few multiplications
written out longhand, which is clearer than bending the tilemap API around a
non-square cell.

## 1. Window and geometry

`@WIN_W` / `@WIN_H` are read straight out of the source text by
`scan_lux_win_size` (`src/cloister.c:38`), which sizes the SDL window before the
ROM boots. The app then reads the same numbers back off `/dev/draw` through
`APP::width` / `APP::height`, and draws from *those*, so a restart from the ESC
menu re-derives its layout rather than trusting the constants.

| Constant | Value | Note |
|---|---|---|
| `WIN_W` × `WIN_H` | 512 × 480 | compact arcade window |
| `BAR_H` | 28 | status bar, as in Snake |
| `WALL` | 16 | left/right playfield margin |
| `COLS` × `ROWS` | 10 × 6 | 60 bricks |
| `BRICK_W` × `BRICK_H` | 48 × 16 | 10 × 48 = 480 = 512 − 2×16, exactly |
| `BRICK_TOP` | 68 | `BAR_H + 40` |
| `PADDLE_W` / `PADDLE_H` | 64 / 8 | width shrinks 8px per level, floor 32 |
| `PADDLE_Y` | 440 | `WIN_H − 40` |
| `BALL` | 8 | a **square** ball |

The ball is square both because that is what the 1972 cabinet drew and because
`DRAW::paint-oval` emits one `fill-rect` per scanline — eight draw commands per
frame for a shape that is eight pixels across.

## 2. Memory

State lives at **`0x880000`**, a free slice of the app small-state band
(`docs/memory-map.md`). The occupied addresses in that band are `0x800000`,
`0x803000`, `0x8A0000` (Snake / UIDemo / Whittle), `0x8B0000` (`lib/sf.lux`),
`0x8C0000` (`lib/app.lux`), `0x8D1000` (`lib/tilemap.lux`), `0x8D2000`
(`lib/ninep.lux`), `0x8E….` (`lib/cff.lux`, `lib/ui.lux`) and `0x8F….`
(`lib/draw.lux`); `0x880000` collides with none of them.

The brick array is at `0x881000`, **one 32-bit word per brick** — 60 bricks,
240 bytes. Words rather than bytes so it is plain `LOADI`/`STOREI`: byte access
is not a VM opcode and `lib/core.lux` has to synthesize it from an aligned
load/mask/shift/store, which is not worth it to save 180 bytes.

`0` means the cell is empty; non-zero is the brick's row+1, which is also what
selects its fill pattern.

## 3. Fixed-point motion

**8 fractional bits** — one unit is 1/256 px. Ball position (`bx`, `by`) and
velocity (`vx`, `vy`) are all stored scaled by 256; the code divides by `FP`
(`@ball-l` / `@ball-t`) to recover whole pixels for drawing and collision.

Division rather than `8 ARSHIFT`: NUX division truncates toward zero while an
arithmetic shift floors, and the two disagree by one pixel on negatives. It
makes no difference for positions, which are always positive, but `vx / n` in
`@advance` is signed, and truncation there keeps a leftward sub-step exactly
the mirror of a rightward one. All NUX arithmetic and comparison is signed
32-bit; plain `RSHIFT` anywhere in this file would turn an upward-moving ball
into a very large downward one.

Range is not a concern: 512 px scaled by 256 is 131 072, five orders of
magnitude inside `INT32_MAX`.

Base speed is `SPEED = 576` — 2.25 px per *simulation step*, not per rendered
frame; see §7 — and gains 64 per level.

### Paddle deflection

Where the ball lands along the paddle sets the outgoing angle. This is the
whole game; a paddle that reflects straight makes Breakout unplayable.

```
off = ball_centre_x − paddle_centre_x        ( −PADDLE_W/2 .. +PADDLE_W/2 )
vx  = off × SPEED × 2 / PADDLE_W             ( scaled to roughly ±SPEED )
vy  = −(SPEED − |vx| / 2)                    ( always negative: upward )
```

Deriving `vy` by subtraction rather than as `sqrt(SPEED² − vx²)` keeps the
overall speed near-constant without a square root — there is no sqrt opcode, and
the approximation is imperceptible in play. `|vy|` is clamped to a floor so the
ball can never end up travelling almost horizontally and stall between the walls
forever.

### Sub-stepping

Each tick the ball moves in `n = (speed / FP) + 1` sub-steps, and within each
sub-step it advances **x, tests, then y, tests**. The per-sub-step deltas are
read out of `vx`/`vy` inside the loop rather than computed once up front: a
bounce flips a velocity mid-tick, and the sub-steps after it have to travel the
new direction.

Two reasons, both load-bearing:

- **Tunnelling.** A brick is 16 px tall. Once the ball is quick enough to move
  more than that in one tick it would pass clean through a brick without ever
  occupying it.
- **Reflection axis.** Moving one axis at a time makes the bounce direction
  unambiguous — whichever axis moved into the brick is the axis that flips. Move
  both at once and a corner hit gives you no way to tell which face was struck.

## 4. Collision

Tested after each axis moves, within each sub-step:

| Surface | Response |
|---|---|
| Left / right wall | clamp to the wall, negate `vx`, beep 165 |
| Ceiling (`y < BAR_H`) | clamp, negate `vy`, beep 165 |
| Floor (`y > WIN_H`) | **not** a bounce — lose a life, beep 110, go to `S_SERVE`, or `S_OVER` at zero lives |
| Paddle | only when `vy > 0` (falling) and the x-spans overlap; apply the deflection above, beep 220 |
| Brick | clear the cell, `bricks-left − 1`, score, negate the axis that just moved, beep `440 + row×40` |

Bricks are scored `(ROWS − row) × 10`, so the back rows — the ones you have to
work for — are worth more.

At most one brick is cleared per sub-step: after a hit the test stops, so
clipping a corner takes out one brick rather than two. When `bricks-left`
reaches 0 the state becomes `S_CLEARED`.

## 5. States and input

```
S_TITLE 0    S_SERVE 1    S_PLAY 2    S_CLEARED 3    S_OVER 4
```

**Held keys, not keypresses.** `on-kbd` sets `kleft` / `kright` on
`EVENT::KEY_DOWN` and clears them on `EVENT::KEY_UP`; `on-frame` moves the
paddle while a flag is set (on `on-tick`, so its speed is in px per step). Moving per keypress instead
would put the paddle at the mercy of the OS auto-repeat delay — a long stall
before the second step — which is fatal in a game where the paddle has to track
a moving ball.

`a` / `d` (97 / 100) work alongside the arrows, as in Snake. Space or Enter
serves.

This is the app that forced two fixes underneath it:

- The SDL front end queued key-up as type 1 (`src/cloister.c`) but the fan-out
  into the guest's keyboard queue only forwarded key-*down*, so a release never
  reached any app. Nothing noticed because nothing before Breakout cared, and
  the headless tests write packets straight into the kbd channel, bypassing the
  host entirely. `system_push_host_event` (`src/system.c`) now owns that
  fan-out precisely so a test can reach it — see `test_host_event_fanout` in
  `src/test_vfs.c`.
- `APP::loop` used to read exactly **one** keyboard packet per frame (a `?`,
  not the `|:` drain it uses for the mouse). A held key is a down/up *pair* and
  auto-repeat arrives in bursts, so the 64-slot queue could back up into
  visible lag. The keyboard is now drained like the mouse.

In `S_SERVE` the ball rides on the paddle and moves with it, launching upward at
a slight angle toward `serve-side`, which alternates each serve.

## 6. Levels

Clearing the wall shows a banner and a **Next Level** button, then:

- `level + 1`
- `SPEED + 64`, capped at 1408 (5.5 px/step) — a faster ball, but only up to
  the point where it stops being a game
- `PADDLE_W − 8`, floor 32 — a narrower paddle
- a refilled brick array
- back to `S_SERVE`, lives carried over

Layouts are generated rather than authored: level 1 is six solid rows, and later
levels punch gaps with `(col + row + level) MOD n`. That gives every level a
visibly different wall without a table of hand-drawn maps, and it never
generates an unwinnable board because gaps only ever remove bricks.

## 7. The game loop

Breakout registers **two** hooks, not one:

- `APP::on-tick!` — paddle, ball, collisions, level clear. Runs on a fixed
  **16 ms** step (`APP::STEP_MS`, ~60 Hz).
- `APP::on-frame!` — `paint`, and nothing else. Runs once per rendered frame.

`APP::loop` keeps a millisecond accumulator fed from `/dev/time`'s monotonic
field and runs however many whole steps the elapsed time owes — usually one,
sometimes zero, occasionally several. A frame that took 33 ms to draw runs two
steps rather than running one and quietly halving the game's speed. That is the
whole reason for the split: the host paces itself to 60 fps with `SDL_Delay`
(`FRAME_TARGET_MS`, `src/cloister.c`, vsync off by default), so a render frame
is *approximately* 16 ms and never exactly it.

Two guards keep the catch-up honest:

- `MAX_DT` (100 ms) clamps a single frame's elapsed time, so resuming from the
  ESC menu or a frame that blew its cycle budget does not dump seconds of
  simulation into one frame.
- `MAX_STEPS` (5) caps the steps one frame may run, and a frame that hits the
  cap **drops** its remaining backlog rather than carrying it forward — a
  carried backlog on a machine that cannot keep up would cap out every frame
  forever.

An app that registers no `on-tick` never reads the clock and behaves exactly as
it did before, which is why Easel, Tabula, Quill and the rest are untouched.

### Interpolated drawing

A 16 ms step never divides a ~16.67 ms frame, so the steps do not line up with
the frames: about every 24th frame runs **two** steps. Painting the raw
simulation state made that frame move the ball twice as far as its neighbours —
measured at 5 px against a usual 2–3 — which reads as a lurch a couple of times
a second.

So `paint-field` does not draw where the last step left the ball. It draws
between the previous step and the current one:

```forth
pbx LOADI bx LOADI APP::lerp FP / dbx STOREI
```

`APP::tick-alpha` is how far into the current step the renderer is (0–255, the
unspent accumulator over `STEP_MS`), and `APP::lerp` applies it. This renders
one step *behind* real simulation state, and in exchange the ball advances the
same distance every frame however the steps fell. `@sync-prev` collapses the
window whenever the ball is teleported rather than moved — losing a life,
starting a level — so it never streaks across the screen for a frame.

Ball positions stay in fixed point until *after* the interpolation (`APP::lerp`
then `FP /`, not the other way round), or the fraction being interpolated would
have been rounded away first.

What remains is the ±1 px of integer rounding: 2.34 px/frame draws as a
repeating 2, 3, 2, 2, 3. That is the floor for a renderer with no sub-pixel
coverage, and `test_ball_render_smoothness` pins it — every frame moves 2 or 3
px, never 5.

`src/test_compiler.c`'s `test_app_fixed_timestep` pins the step logic down by
freezing `/dev/time` (`system_freeze_monotonic_ms`) and stepping it by hand:
the headless harness runs `machine_tick` far faster than real time, so without
a controllable clock a simulation step would never fire under test.

## 8. Presentation

Full repaint every frame — it is an animated game, so there is no such thing as
a clean frame to skip. Clear to white, then:

- **Status bar**: Score / Lives / Level / Best, Chicago, via `DRAW::draw-str`
  and `STR::int-to-str`.
- **Playfield**: a black border rectangle inset by `WALL`.
- **Bricks**: each row is a solid `DRAW::fill-rect` in a different k8 gray
  (`DRAW::gray`, luma `0, 36, 72, 108, 144, 180` from the top), outlined in
  black. The app calls `APP::grayscale!` so those values stay gray even if a
  later fill is written as RGB.
- **Paddle and ball**: solid black rects.

That is roughly 70 draw commands per frame, comfortably inside budget — a full
Easel repaint is 4.3M instructions against a 1M-cycle-per-tick cap with
host-side catch-up (`src/cloister.c`).

The ESC menu (Continue / Restart / Quit) comes free from `lib/app.lux`; the
simulation is paused while `APP::esc-open?` is true.

The high score is kept in `/sys/file/.breakout_hi`, written on a new best,
following Snake's `@load-best` / `@save-best` exactly.

## Verification

```bash
make apps/Breakout.bin
make test
./bin/cloister apps/Breakout.bin
```

Then play a round:

1. Arrows move the paddle smoothly and it stops dead at both walls. Press one
   arrow, release it, then press the other — the paddle must keep responding.
   (Before the key-up fix in §5 it froze permanently at this step, because both
   held-key flags were stuck on and cancelled each other out.)
2. Space serves. The ball bounces off walls, ceiling and paddle, and the
   outgoing angle changes with where on the paddle it lands.
3. Bricks vanish on contact, the ball reflects on the correct axis, score rises.
4. Dropping the ball costs a life; the third loss gives GAME OVER + Play Again.
5. Clearing the wall advances the level — faster ball, narrower paddle, a new
   layout, lives carried over.
6. ESC opens the system menu and the ball freezes while it is open.
7. Quit and relaunch: the high score persisted.
8. `./bin/cloister` with no argument lists Breakout in the Picker — it is picked
   up from `apps/*.bin` automatically, with no registration step.
9. `NUXVM_VSYNC=1 ./bin/cloister apps/Breakout.bin` plays at the same speed as
   without it. That is the point of §7: the ball is on a wall clock, not on the
   frame rate.
