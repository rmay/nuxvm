# Road Escape — design notes

`apps/RoadEscape.lux`. A top-down road chase in eight shades of gray. Arrows
steer and set your speed, Space fires, three lives, and the road never ends —
but the tank and the magazine do, so the cans and crates on the tarmac are the
only reason the run continues.

```
./bin/cloister apps/RoadEscape.bin
```

## Context

Breakout brought fixed-point motion and a fixed-timestep loop into the guest
(`docs/games/breakout_clone.md`). Road Escape reuses both and adds the one
thing neither Snake nor Breakout has: a **world larger than the window**. The
road scrolls forever, so the app needs a representation that can be extended
at one end and forgotten at the other without the per-frame cost growing.

Everything else is by now the house style: fixed-address globals,
`UI::overlay-button` for the between-round buttons, a high score in
`/sys/file`, beeps on `/dev/audio`, and `APP::on-tick!` / `APP::on-frame!`
split so the car covers the same ground per second whatever the frame rate.

## 1. Window and geometry

`@WIN_W` / `@WIN_H` are read out of the source by `scan_lux_win_size`
(`src/cloister.c:38`) to size the SDL window before the ROM boots. The app
then lays out from `APP::width` / `APP::height`, so an ESC-menu restart
re-derives.

| Constant | Value | Note |
|---|---|---|
| `WIN_W` × `WIN_H` | 448 × 480 | a tall, narrow arcade window |
| `BAR_H` | 44 | two rows: the run's tally over the car's dashboard |
| `SLICE_H` | 8 | one road slice |
| `NSLICE` | 92 | enough slices to floor a 720px-tall canvas |
| `ROAD_W` / `KERB_W` | 200 / 4 | constant width; only the kerb's x moves |
| `SHOULDER` | 24 | how close the kerb may come to the window edge |
| `CAR_W` × `CAR_H` | 20 × 32 | player and traffic are the same size |
| `CAR_GAP` | 72 | the player's inset from the bottom |
| `PIK_W` × `PIK_H` | 18 × 18 | cans and crates are the same box |

`NSLICE` is sized for 720px rather than for `WIN_H` because the headless test
canvas is 960×720 (`lux_app_machine`, `src/test_compiler.c`), and a road that
only reached 480px would leave the bottom of that canvas unpaved.

## 2. Memory

State lives at **`0x870000`**, a free slice of the app small-state band below
Breakout's `0x880000`; see `docs/memory-map.md` for the occupied addresses it
clears. The road ring is at `0x871000` (92 words), traffic at `0x871400`
(8 × 32 bytes), bullets at `0x871800` (6 × 16 bytes) and pickups at
`0x871C00` (4 × 32 bytes).

Records are word-per-field for the same reason Breakout's brick array is:
byte access is not a VM opcode, and `lib/core.lux` has to synthesize it from
an aligned load/mask/shift/store.

## 3. The road is a ring, not an array

The obvious way to scroll a road is an array of slices that shifts down by one
each time you travel `SLICE_H`. That is 92 word-copies per shift, and it does
the same work forever.

Instead the road is a **ring buffer with a moving head**. `@head` is the index
of the newest — topmost — slice; slice *j* down the screen lives at
`(head + j) MOD NSLICE`. Pushing a slice decrements `head` (mod `NSLICE`) and
writes one word, overwriting the cell that just scrolled off the bottom. One
store, whatever the ring's length.

Slice *j* is drawn at

```
y = play_t + j*SLICE_H - SLICE_H + scroll        scroll in [0, SLICE_H)
```

so slice 0 sits one whole slice above the playfield and slides into view as
`scroll` grows. At `scroll == SLICE_H` the picture is identical to `scroll == 0`
with `head` advanced by one, which is what makes the seam invisible.

`scroll` is not a stored counter. `@dist` is the fixed-point distance
travelled, monotonic, and both the scroll (`dist MOD SLICE_FP`) and the number
of slices owed (`dist / SLICE_FP`, against `@pushed`) fall out of it. One
number drives the whole scroll, which is what lets the *drawing* interpolate
it — see §7. `dist` overflows `INT32_MAX` after about eight hours at top
speed; nothing else in the app cares.

### Generating road

`@push-slice` walks the kerb 2px at a time toward `@rtgt`, a target picked at
random every 20–40 slices inside `[SHOULDER, width − ROAD_W − SHOULDER]`. The
road therefore curves at a bounded rate, is always exactly `ROAD_W` wide, and
is always fully on screen. `@fill-road` lays 30 straight slices before the
first target change, which is what gives the opening of a round — and
`test_roadescape_throttle` — a stretch of road that does not move under you.

The curve rate is load-bearing: at 2px per slice the road drifts at most
0.5–1.5 px per simulation step, against 4px per step of steering. Steering
always wins, so no curve is unavoidable.

## 4. Traffic rides the kerb, not the screen

An enemy record stores **`ox`, an offset from the left kerb**, not an absolute
x. `@enemy-x` resolves it against the kerb of whatever slice the car is
currently in:

```
enemy_x = kerb_at(enemy_y) + ox
```

So traffic follows the curve for free, and it can never be spawned off the
road: `ox` is drawn from `0 .. ROAD_W − CAR_W`. Nothing in the app steers a
car sideways.

Each car carries a **world speed**, and what you see is the difference between
it and yours:

```
enemy_y += player_speed − enemy_speed
```

Cars are slower than you, so flooring the throttle is what brings them toward
you — which is the correct reading of a chase from behind, and it makes the
throttle the game's real control rather than a number in the status bar.

## 5. Fuel and ammunition

The road is endless, so without something that runs out there is no reason to
stop driving and nothing to weigh a decision against. Two consumables supply
that, and neither is restored by a wreck — only a new game refills them, which
is what makes a can worth crossing two lanes for.

**Fuel** burns with *distance*, not time:

```
fuel -= speed/FP + 1        per simulation step
```

so the throttle is what costs you and cruising is the frugal choice. A full
`FUEL_MAX` of 12000 is about 66 seconds at `CRUISE` and about 28 flat out.
Running dry ends the run outright, lives or no lives — there is nothing left
to drive with — and the banner says `OUT OF FUEL` rather than `GAME OVER`, off
a `@dry` flag set by `@burn-fuel` and cleared by `@crash`.

Charging fuel to distance rather than to steps is the whole reason the
throttle is a decision. Charge it to time and flooring it is strictly better:
you cover more ground per unit burnt. Charge it to distance and speed costs
exactly what it buys, leaving the real trade-off — a fast lap is a short one,
and dodging is harder at 6px a step.

**Ammunition** starts at 20 and caps at 99. `@fire` refuses at zero and still
takes the `FIRE_GAP` cooldown, so holding Space on an empty gun gives one dry
click every 8 steps rather than one per step. Together with the civilian
penalty (§7) that makes two independent reasons not to spray.

### Pickups

Cans and crates share one 4-slot table. Unlike traffic they hold **no speed of
their own** — they are painted on the tarmac, not driven — so they slide down
the screen at exactly yours. Like traffic they store an offset from the kerb,
so they sit in a lane through a curve.

One drops every 140–229 steps. The kind is a coin flip, with one thumb on the
scale: below `FUEL_LOW` the next drop is *forced* to be a can. An empty tank
is a loss you cannot shoot your way out of, and leaving that to the dice would
make a run end on something the player could not have played around.

A crate gives `AMMO_CRATE` 25 rounds — more than the magazine starts with, so
one is worth leaving your lane for — and a can gives `FUEL_CAN` 4000, a third
of a tank; both cap rather than overflow. Both are collected by plain
rectangle overlap with the player's car, and bullets pass straight over them.

## 6. Fixed point and motion

8 fractional bits, one unit = 1/256 px, as in Breakout. Division rather than
`ARSHIFT` throughout: NUX division truncates toward zero while an arithmetic
shift floors, and all NUX arithmetic is signed — a plain `RSHIFT` on a
negative bullet velocity would send it a very long way down the screen.

| Quantity | Value | Per step |
|---|---|---|
| `MIN_SPEED` / `CRUISE` / `MAX_SPEED` | 256 / 512 / 1536 | 1 / 2 / 6 px |
| `ACCEL` | 16 | ~1s from floor to ceiling |
| `STEER` | 1024 | 4 px |
| `SHOT` | 2048 | 8 px, screen-relative |

There is no drag: releasing the throttle holds your speed — what pushes back
against it is the fuel burn (§5), not physics. Bullets move in screen space
rather than world space, so they always outrun traffic whatever you are doing
with the throttle.

No sub-stepping. Breakout needed it because a fast ball could tunnel through a
16px brick; here the fastest closing speed between a bullet and a car is
8 + 6 = 14px per step against a 32px-tall car, so a single move per step can
never step over anything.

## 7. Collision

| Surface | Response |
|---|---|
| Kerb | crash — tested at **both** ends of the car, since the road curves and a nose still on tarmac says nothing about the tail |
| Traffic | crash |
| Bullet × interceptor | destroyed, +100, beep 440 |
| Bullet × civilian | destroyed, **−150**, beep 165 |
| Car × fuel can | +`FUEL_CAN`, capped at a full tank, beep 330 |
| Car × ammo crate | +`AMMO_CRATE`, capped at `AMMO_MAX`, beep 550 |

The civilian penalty and the magazine are what keep Space honest: without
either, holding fire would be free money. Score has a floor of 0
(`@add-score`), and one point accrues per slice travelled, so distance is the
baseline and the gun is the multiplier.

A crash costs a life, stops the world for `CRASH_MS` (45 steps ≈ ¾s) with a
banner up, and respawns centred at `CRUISE` with the traffic — and any
uncollected pickups — cleared. Fuel and ammunition carry straight through it. The
third loss goes straight to GAME OVER without the pause. The high score lives
in `/sys/file/.roadescape_hi`, following Snake and Breakout exactly.

## 8. The game loop

Two hooks, as in Breakout: `APP::on-tick!` for the simulation on a fixed 16ms
step, `APP::on-frame!` for `@paint` and nothing else. `APP::loop` runs however
many whole steps the elapsed time owes, clamped by `MAX_DT` and `MAX_STEPS`
(`lib/app.lux`, and §7 of the Breakout notes for why).

Everything that moves is drawn between the previous step and the current one
via `APP::tick-alpha` / `APP::lerp`: the player's x, each car's y, each
bullet's and pickup's y, and — the reason `@dist` is a single monotonic
number — the road's own scroll. Interpolating the scroll rather than the head index is what keeps
the road from lurching on the ~1-frame-in-24 that runs two simulation steps.

The interpolated scroll can land a hair *behind* a slice push, so the ring's
newest slice is drawn at the top of the screen a frame before the simulation
would say it exists. It is generated as a continuation of the slice below it,
so the seam is invisible; the alternative — interpolating the head index —
has no meaning to interpolate.

## 9. Presentation

Full repaint every frame, ~420 draw commands: 92 slices × (tarmac, two kerbs,
and a centre dash on half of them), plus traffic, pickups, bullets, the player
and the dashboard. `DRAW::batch-begin` is called once at startup so those go to
`/dev/draw` in batches rather than one `VFS::write` each; `DRAW::begin-frame` /
`end-frame` flush it, so nothing else has to.

Eight-bit gray throughout (`APP::grayscale!`, `DRAW::gray`): grass 176,
tarmac 48, kerbs and lane markings 255, civilians 208, interceptors 112, the
player's car white — all outlined in black, with a black windshield band and
black wheels so a light car still reads against the grass and a dark one still
reads against the tarmac.

A can is 224 with a band and a spout, and a crate is 160 stencilled with three
rounds — a 1px tip over a 3px body each — which says what is inside without a
legend. Two pixel-level details decide whether either reads at 18px:

- The spout is drawn in the body's **pale gray, not ink**. Black on 48-gray
  tarmac is very nearly invisible, and the first version's black spout left
  the can looking like a plain box. It is the body's own outline, drawn over
  the spout's foot afterwards, that reads as the collar.
- The rounds are 3px wide on a 2px pitch, and `PIK_W` is 18 rather than 16 for
  no other reason. At 4px wide they left a single pale pixel between them and
  merged into one dark slab; at 16px wide the outermost round touched the
  crate's own outline.

The dashboard is two rows: Score / Lives / Best above, Fuel / Ammo / Spd
below. The gauges multiply before they divide — `FUEL_MAX` is 12000 against a
108px bar, and dividing first would floor every reading to zero. Below
`FUEL_LOW` the fuel gauge flashes instead of merely reading short, driven by
`@blink`, a counter of *simulation steps* rather than frames, so the flash is
on the same wall clock as everything else and does not race the refresh
rate.

The ESC menu (Continue / Restart / Quit) comes free from `lib/app.lux`; the
simulation is paused while `APP::esc-open?` is true.

## 10. Two traps worth writing down

The Lux dictionary is **case-insensitive** (`strcasecmp`, `src/compiler.c:157`)
and **first definition wins**. This app first shipped with a colour constant

```forth
@PAINT ( -- c ) 255 DRAW::gray ;
```

defined above `@paint`, the whole-screen repaint. Every later call to `paint`
resolved to the constant, `@paint` was never reached, and the app ran
perfectly with a completely black window — while all three headless tests,
which read guest memory, stayed green. The constant is now `@WHITE`, and
`test_roadescape_road` ends by asserting that a committed frame actually has
lit pixels in it, which is the assertion that would have caught it.

And the same trap has a second mouth: **builtins are matched before the
dictionary** (`src/compiler.c:850`), also case-insensitively. The pickup
accessor was first written

```forth
@pick { i -- addr } pickup i PIK_STRIDE * + ;
```

and `PICK` is a VM stack opcode, so every `i pick` compiled to a stack `PICK`
and the accessor was never reached. It is `@pik` now. The check is cheap
enough to run by hand against `is_builtin`'s table when adding words to an
app, and both halves of the trap are silent: nothing warns, and the app
compiles clean.

Both traps are silent at compile time. A third failure in the same family is
silent at *run* time: a `DRAW::fill-rect` call one argument short faults the
guest, and `machine_tick` reports that by returning false rather than by
halting the VM — so a test harness that ignores the return pumps a dead
machine and reads state cells frozen at whatever they held when the fault hit,
which looks exactly like a simulation that has stopped moving. The crate art
shipped that way for one build. `app_sim_steps` (`src/test_compiler.c`) now
asserts `halted || running` after every pump, which names the fault on the
step it happens, for all ten tests that share the helper.

## Verification

```bash
make apps/RoadEscape.bin
make test
./bin/cloister apps/RoadEscape.bin
```

Then drive:

1. Left/right (or `a`/`d`) steer, and the car keeps moving while the key is
   held. Up/down (or `w`/`s`) work the throttle; the gauge in the status bar
   tracks it and pins at both ends.
2. Leaving the tarmac at either kerb costs a life and puts WRECKED on screen
   for about three quarters of a second.
3. Space fires, and each shot comes off the Ammo count. Empty the magazine
   and Space gives a dry click every eight steps instead of a bang. A shot
   kills a dark interceptor for +100 and a pale civilian for −150 — watch the
   score, not the explosion.
4. The Fuel gauge falls faster the harder you drive, and flashes below a
   quarter of a tank. Drive over a can (spout and band) to refill a third of
   it and a crate (three rounds stencilled on it) for 25. Let the tank empty
   and the run ends
   with `OUT OF FUEL` however many lives are left.
5. Wreck deliberately with a part-full tank: fuel and ammo carry through the
   respawn, and only Play Again resets them.
6. Traffic appears at the top and comes toward you faster the harder you
   drive. Ramming a car costs a life.
7. The third loss gives GAME OVER + Play Again.
8. ESC opens the system menu and the road freezes while it is open.
9. Quit and relaunch: the high score persisted.
10. `./bin/cloister` with no argument lists Road Escape in the Picker — it is
    picked up from `apps/*.bin` automatically, with no registration step.
11. `NUXVM_VSYNC=1 ./bin/cloister apps/RoadEscape.bin` drives at the same
    speed as without it, and the road scrolls just as smoothly.
