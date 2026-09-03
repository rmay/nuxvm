# Add hover tooltips to the UI toolkit

## Context

The user asked how hard it would be to add hover tooltips (small text labels that appear after hovering over a control for a moment). Exploration found no existing tooltip/hint concept anywhere in the codebase, but every mechanism tooltips need already exists in some form: a per-frame hook, a delay-timer pattern, and an established "draw last, on top" overlay convention. So this is a moderate, well-contained addition — no restructuring of the UI toolkit or event model required.

Scope, per user decision: support tooltips both for generic `UI::button` widgets (shared across Easel, Nib, Quill, Whittle) and for Easel's custom-drawn tool palette icons (which aren't buttons at all, just an indexed grid).

## Key existing mechanisms to reuse

- **Frame loop / per-frame hook**: `lib/app.lux:269-338 @loop` runs every frame unconditionally and calls `v-frame` (`lib/app.lux:325-327`) each iteration regardless of input — this is where "has the mouse been idle over widget X for N ms" gets polled.
- **Mouse event dispatch**: `lib/app.lux:281-290` drains the mouse queue and calls each app's `v-mouse` (e.g. `apps/Easel.lux:4216 @on-mouse`); `MOUSE_MOVE` is checked via `EVENT::mouse-type EVENT::MOUSE_MOVE =` (`lib/ui.lux:691,2281`). There's no generic per-widget hover state today — only a menu-bar-specific `MB_HOVER` flag (`lib/ui.lux:1737`).
- **Delay/timer pattern**: `TIME::milli@` (`lib/uisf.symtab.json:220`) is already used for delay logic, e.g. double-click detection (`lib/ui.lux:1503,1605-1612`, 500ms threshold). Tooltip show-delay (e.g. 500-700ms) follows the same pattern.
- **Draw-on-top / overlay convention**: `lib/ui.lux:3083-3300` (`OV_TAB`/`ov-put`/`ov-find`/`overlay-draw`) is the established floating-UI mechanism, called last in each app's draw routine (e.g. `apps/Easel.lux:2763,2867,2964,3180`). `lib/app.lux:332-334` shows the simpler pattern (`esc-open`/`draw-esc-popup`) drawn as the loop's final "Handle Overlay" step. A tooltip popup reuses this same "drawn last, after the app's own frame" slot.
- **Button struct**: generic `UI::button` is a fixed 36-byte struct (`lib/ui.lux:180-205`, fields `BTN_X..BTN_DEFAULT`) with no spare field.
- **Easel tool palette**: custom-drawn grid, not `UI::button` — indices via `TOOL_*` constants (`apps/Easel.lux:116-135`), hit-testing via `hit-tool`/`tool-xy`/`tool-rect` (`apps/Easel.lux:1962-1971`), drawn via `draw-tool-cell`/`draw-palette` (`apps/Easel.lux:3401-3442`).

## Implementation plan

1. **Track hover state.** Add module-level "last hovered id + timestamp" state in `lib/ui.lux` (or per-app where the palette lives). On `MOUSE_MOVE`, update it: if the hovered widget/cell id changed, reset the timestamp via `TIME::milli@`; if it's unchanged, leave the timestamp alone.

2. **Extend `UI::button`.** Add a `BTN_TOOLTIP` field (pointer/offset to a label string) to the button struct, update `button-init` and its call sites to accept an optional tooltip argument (empty/null = no tooltip).

3. **Add a palette label table for Easel.** Since the tool palette isn't button-based, add a small `TOOL_* -> label string` lookup table alongside the existing `TOOL_*` constants (`apps/Easel.lux:116-135`).

4. **Poll idle-hover in the per-frame hook.** In `v-frame` (or a new small shared helper called from each app's `v-frame`), check `TIME::milli@ - hover-start-time` against a threshold (e.g. 600ms); when exceeded and a tooltip string exists for the hovered id, set a "show tooltip" flag with the widget's screen rect and label.

5. **Draw the tooltip as an overlay.** Add a lightweight `UI::tooltip-draw` (small rounded rect + text, positioned near the cursor/widget, clamped to screen bounds) called in the same "last, drawn on top" slot as `overlay-draw`/`draw-esc-popup`. Suppress it immediately on mouse movement past a small tolerance, click, or hover-id change.

6. **Wire into each app.** Add tooltip strings to Easel's palette table and to a first pass of `UI::button` call sites (start with Easel, Nib, Quill, Whittle toolbars/menus as time allows — this is repetitive, low-risk work once steps 1-5 land).

## Verification

- Build and run one app (Easel) via the project's run skill; hover over a palette tool and a `UI::button` (e.g. a dialog button) and confirm the tooltip appears after the delay, tracks/dismisses correctly on move, and doesn't appear on quick mouse-throughs.
- Check no regression in existing hover-adjacent behavior (menu-bar `MB_HOVER`, double-click timing in listboxes) since they share the `TIME::milli@` idiom but are otherwise independent state.
- Spot-check tooltip rendering doesn't clip off-screen near window edges.
