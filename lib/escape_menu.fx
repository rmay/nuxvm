/* Generalized escape/pause menu for Fluxio apps.
 *
 * Pressing Esc opens the same System 6 "System Menu" sheet Lux apps get
 * from APP:: (white 200x160 panel, 2px black frame, Continue / Restart
 * App / Quit). Resume is Continue: it closes the sheet. Restart App
 * closes it and sets a flag the app checks to reset. Quit sets a flag
 * the app's main loop checks to exit. Pure Fluxio -- fill_rect/draw_str
 * only, no extern/ABI link required -- so any app standardizes on the
 * same pause menu with just:
 *
 *     include "lib/escape_menu.fx";
 *
 *     escmenu_init(canvas_w, canvas_h);        // once, after canvas_size()
 *     ...
 *     if (!escmenu_kbd(kbd_type(), kbd_key())) { ... } // app's own key
 *                                                // handling, skipped while
 *                                                // the menu is open or just
 *                                                // toggled
 *     if (!escmenu_mouse(mtype, mbtn, mx, my)) { ... } // same for mouse
 *     ...
 *     escmenu_draw(fd);                         // last, after the app's
 *                                                // own frame content
 *     if (escmenu_wants_quit()) { break; }       // in the main loop's
 *                                                 // exit check
 *     if (escmenu_wants_restart()) { ...; escmenu_ack_restart(); }
 *
 * Esc (key code 27) toggles open/closed. While open, Up/Down move the
 * default ring and Enter fires the highlighted button. escmenu_kbd/
 * escmenu_mouse both consume (return 1) everything while the menu is
 * open, so a caller gates its own input handling on the return value.
 *
 * The look is not an approximation of the Lux overlay -- it is a port of
 * it, and the two are required to stay pixel-identical. APP::draw-esc-popup
 * paints the panel, then UI::overlay-draw runs UI::button-draw per button,
 * which composes DRAW::paint-rrect-6 / frame-rrect-6 (System 6 rounded
 * rects, ovalWidth/Height 6) for the face, three nested rounded frames for
 * the default ring, and a label centred on its DRAW::str-w width at
 * h/2 - 7. Everything below reproduces that, including the inverted face
 * while a button is held and the ring following the mouse. Colours are
 * UI::CLR_BG / CLR_BORDER / CLR_TEXT verbatim.
 *
 * test_escape_menu_matches_lux_pixel_for_pixel (src/test_fluxio_compiler.c)
 * renders both sheets and diffs the whole panel region on every `make
 * test`, so a change here that drifts from lib/ui.lux or lib/draw.lux fails
 * the build rather than quietly looking wrong.
 *
 * This library selects no font. draw_str uses whatever face is current, and
 * the Lux original explicitly sets Chicago first, so an app that switches
 * faces must set Chicago (face 0) back before escmenu_draw -- Quill.fx
 * does.
 *
 * Feed keys through escmenu_kbd(kbd_type(), kbd_key()), NOT through
 * escmenu_key(). /dev/kbd delivers a KEY_UP (type 1) after every KEY_DOWN
 * (type 0, include/system.h), and since Esc *toggles*, handing the library
 * both halves of one physical keypress opens the menu on the down and
 * closes it again on the up -- the sheet never survives to be drawn. That
 * was a real bug in this library's own reference caller
 * (apps/fluxio/HelloCloister.fx), which is why the type filter now lives
 * here instead of being every app's job to remember.
 */

int escmenu_open;
int escmenu_quit;
int escmenu_restart;
int escmenu_hov;
/* Index of the button the mouse is currently pressing, -1 for none. Lux's
 * UI::button-update keeps this as BTN_STATE == 2 and inverts the face while
 * it holds; the click itself posts on release inside the same button. */
int escmenu_press = -1;
int escmenu_canvas_w;
int escmenu_canvas_h;

/* Match lib/app.lux @draw-esc-popup / @esc-build: 200x160 sheet, 120x20
 * buttons at y+50 / y+78 / y+106. */
int escmenu_panel_w = 200;
int escmenu_panel_h = 160;
int escmenu_btn_w = 120;
int escmenu_btn_h = 20;
int escmenu_btn_gap = 8;

/* UI::CLR_BG / CLR_BORDER / CLR_TEXT (lib/ui.lux) verbatim. */
int escmenu_clr_panel = 0xFFFFFF;
int escmenu_clr_border = 0x000000;
int escmenu_clr_text = 0x000000;

/* Chicago advance widths of the three fixed labels, i.e. what DRAW::str-w
 * returns for them (the sum of the per-glyph widths in /sys/font/widths).
 * UI::button-label centres by that measured width, so the sheet needs the
 * same numbers to land the text on the same pixels; Fluxio has no
 * text-measuring builtin to compute them at runtime, and these labels are
 * fixed literals, so they're pinned here. Change a label, change its width
 * -- lib/ui.lux's own value is the authority. */
int escmenu_label_w_continue = 58;
int escmenu_label_w_restart = 77;
int escmenu_label_w_quit = 26;

/** Resets and centers the menu for a canvas of size w x h -- call once, right after canvas_size(fd). */
int escmenu_init(int w, int h) {
    escmenu_canvas_w = w;
    escmenu_canvas_h = h;
    escmenu_open = 0;
    escmenu_quit = 0;
    escmenu_restart = 0;
    escmenu_hov = 0;
    escmenu_press = -1;
    return 0;
}

/** internal: left edge of the centered panel */
int escmenu_panel_x() {
    return (escmenu_canvas_w - escmenu_panel_w) / 2;
}

/** internal: top edge of the centered panel */
int escmenu_panel_y() {
    return (escmenu_canvas_h - escmenu_panel_h) / 2;
}

/** internal: left edge of the buttons (they share a common center) */
int escmenu_btn_x() {
    return escmenu_panel_x() + 40;
}

/** internal: top edge of button `i` (0 = Continue, 1 = Restart App, 2 = Quit) */
int escmenu_btn_y(int i) {
    return escmenu_panel_y() + 50 + i * (escmenu_btn_h + escmenu_btn_gap);
}

/** internal: true (1) if (px, py) falls within button `i`'s rect */
int escmenu_hit_btn(int i, int px, int py) {
    int bx = escmenu_btn_x();
    int by = escmenu_btn_y(i);
    if (px < bx) { return 0; }
    if (px >= bx + escmenu_btn_w) { return 0; }
    if (py < by) { return 0; }
    if (py >= by + escmenu_btn_h) { return 0; }
    return 1;
}

/** internal: fire button `i` -- 0 Continue, 1 Restart App, 2 Quit */
int escmenu_fire(int i) {
    if (i == 0) {
        escmenu_open = 0;
    } else {
        if (i == 1) {
            escmenu_open = 0;
            escmenu_restart = 1;
        } else {
            escmenu_quit = 1;
        }
    }
    return 0;
}

/**
 * Feeds one /dev/kbd event through the menu, ignoring anything that isn't
 * a KEY_DOWN (type 0) -- this is the entry point apps should use. A KEY_UP
 * is still *consumed* (returns 1) while the menu is open, so a paused app
 * doesn't act on the release half of a keypress it never saw pressed.
 */
int escmenu_kbd(int type, int key) {
    if (type != 0) {
        return escmenu_open;
    }
    return escmenu_key(key);
}

/**
 * Feeds one keydown code through the menu. Prefer escmenu_kbd(), which
 * filters event types for you; callers that have already established the
 * event is a keydown can use this directly. Esc (27) toggles open/closed
 * and is always consumed (the app shouldn't act on the Esc that just
 * opened or closed the menu either); every other key is consumed only
 * while the menu is open. Up/Down move the default ring; Enter fires it.
 * Returns 1 if consumed, 0 if the caller should handle the key itself.
 */
int escmenu_key(int key) {
    if (key == 27) {
        if (escmenu_open) {
            escmenu_open = 0;
        } else {
            escmenu_open = 1;
            escmenu_hov = 0;  /* Continue, same as esc-build's `1 esc-btn-hov` */
        }
        escmenu_press = -1;
        return 1;
    }
    if (escmenu_open) {
        if (key == 13) {
            escmenu_fire(escmenu_hov);
        } else {
            if (key == 17) {
                escmenu_hov = escmenu_hov - 1;
                if (escmenu_hov < 0) {
                    escmenu_hov = 2;
                }
            } else {
                if (key == 18) {
                    escmenu_hov = escmenu_hov + 1;
                    if (escmenu_hov > 2) {
                        escmenu_hov = 0;
                    }
                }
            }
        }
        return 1;
    }
    return 0;
}

/** internal: index of the button under (px, py), or -1 for none */
int escmenu_hit_which(int px, int py) {
    if (escmenu_hit_btn(0, px, py)) {
        return 0;
    }
    if (escmenu_hit_btn(1, px, py)) {
        return 1;
    }
    if (escmenu_hit_btn(2, px, py)) {
        return 2;
    }
    return -1;
}

/**
 * Feeds one mouse event (same mtype/mbutton/mx/my shape as poll_mouse()'s
 * mouse_type()/mouse_button()/mouse_x()/mouse_y() accessors) through the
 * menu, and consumes (returns 1) everything while open. Returns 0 while
 * closed.
 *
 * Mirrors what APP::handle-esc-mouse does with UI::overlay-feed /
 * overlay-handle / overlay-pick:
 *   - any event over a button moves the default ring onto it (overlay-pick
 *     re-runs UI::overlay-default on whatever the mouse is over, so in Lux
 *     the ring follows the pointer, it isn't only moved by Up/Down);
 *   - a button-down (mtype 3) on a button presses it, drawing the inverted
 *     face, and the click posts on the button-up (mtype 4) inside that same
 *     button -- UI::button-update's WAS_DOWN rule, i.e. press-then-release,
 *     so dragging off a button cancels it like a real Mac push button;
 *   - a button-down anywhere else closes the sheet, which is
 *     overlay-held? being false in handle-esc-mouse: docs/user-manual.md's
 *     "Esc or a click outside Continues".
 * Quit leaves the sheet open and sets escmenu_wants_quit(); the app's main
 * loop is expected to see that flag and exit before drawing another frame.
 */
int escmenu_mouse(int mtype, int mbutton, int mx, int my) {
    if (!escmenu_open) {
        return 0;
    }
    int hit = escmenu_hit_which(mx, my);
    if (hit >= 0) {
        escmenu_hov = hit;
    }
    if (mtype == 3) {
        if (hit >= 0) {
            escmenu_press = hit;
        } else {
            escmenu_open = 0;
            escmenu_press = -1;
        }
    } else {
        if (mtype == 4) {
            int was = escmenu_press;
            escmenu_press = -1;
            if (was >= 0) {
                if (hit == was) {
                    escmenu_fire(was);
                }
            }
        }
    }
    return 1;
}

/** True (1) once Quit has been clicked -- check in the main loop's exit condition. */
int escmenu_wants_quit() {
    return escmenu_quit;
}

/** True (1) once Restart App has been chosen -- the caller should reset, then escmenu_ack_restart(). */
int escmenu_wants_restart() {
    return escmenu_restart;
}

/** Clears the Restart App flag after the caller has reset. */
int escmenu_ack_restart() {
    escmenu_restart = 0;
    return 0;
}

/** True (1) while the menu is open -- callers can use this to pause their own game/animation logic. */
int escmenu_is_open() {
    return escmenu_open;
}

/** internal: one pixel, DRAW::pix's 1x1 fill-rect */
int escmenu_pix(int fd, int x, int y, int color) {
    fill_rect(fd, x, y, 1, 1, color);
    return 0;
}

/* The System 6 rounded rectangle, ovalWidth/Height 6. These two are
 * line-for-line ports of DRAW::paint-rrect-6 / DRAW::frame-rrect-6
 * (lib/draw.lux) -- the exact stair-step the real UI library rasterises,
 * not an approximation of it. UI::button-face draws its buttons through
 * paint-rrect/frame-rrect at radius 6, and for a 120x20 button both take
 * the r=6 fast path, so matching these two functions is what makes the
 * Fluxio sheet pixel-identical to the Lux one instead of square-cornered.
 * Keep them in step with lib/draw.lux. */

/** internal: filled rounded rect, DRAW::paint-rrect-6 */
int escmenu_paint_rrect6(int fd, int x, int y, int w, int h, int color) {
    if (h > 6) {
        fill_rect(fd, x, y + 3, w, h - 6, color);
    }
    fill_rect(fd, x + 3, y,         w - 6, 1, color);
    fill_rect(fd, x + 2, y + 1,     w - 4, 1, color);
    fill_rect(fd, x + 1, y + 2,     w - 2, 1, color);
    fill_rect(fd, x + 1, y + h - 3, w - 2, 1, color);
    fill_rect(fd, x + 2, y + h - 2, w - 4, 1, color);
    fill_rect(fd, x + 3, y + h - 1, w - 6, 1, color);
    return 0;
}

/** internal: rounded rect outline, DRAW::frame-rrect-6 */
int escmenu_frame_rrect6(int fd, int x, int y, int w, int h, int color) {
    fill_rect(fd, x + 3,     y,         w - 6, 1,     color);
    fill_rect(fd, x + 3,     y + h - 1, w - 6, 1,     color);
    fill_rect(fd, x,         y + 3,     1,     h - 6, color);
    fill_rect(fd, x + w - 1, y + 3,     1,     h - 6, color);
    escmenu_pix(fd, x + 2,     y + 1,     color);
    escmenu_pix(fd, x + 1,     y + 2,     color);
    escmenu_pix(fd, x + w - 3, y + 1,     color);
    escmenu_pix(fd, x + w - 2, y + 2,     color);
    escmenu_pix(fd, x + 1,     y + h - 3, color);
    escmenu_pix(fd, x + 2,     y + h - 2, color);
    escmenu_pix(fd, x + w - 2, y + h - 3, color);
    escmenu_pix(fd, x + w - 3, y + h - 2, color);
    return 0;
}

/** internal: DRAW::str-w of button `i`'s label */
int escmenu_label_w(int i) {
    if (i == 0) {
        return escmenu_label_w_continue;
    }
    if (i == 1) {
        return escmenu_label_w_restart;
    }
    return escmenu_label_w_quit;
}

/** internal: the three nested rounded frames of UI::button-ring */
int escmenu_draw_ring(int fd, int bx, int by, int w, int h) {
    escmenu_frame_rrect6(fd, bx - 4, by - 4, w + 8, h + 8, escmenu_clr_border);
    escmenu_frame_rrect6(fd, bx - 3, by - 3, w + 6, h + 6, escmenu_clr_border);
    escmenu_frame_rrect6(fd, bx - 2, by - 2, w + 4, h + 4, escmenu_clr_border);
    return 0;
}

/**
 * internal: one System 6 push button, the composition UI::button-draw
 * performs -- default ring first if this is the highlighted button, then
 * the face (inverted while held), then the label centred on its measured
 * width at h/2 - 7 from the top, in the background colour when inverted.
 */
int escmenu_draw_btn(int fd, int i) {
    int bx = escmenu_btn_x();
    int by = escmenu_btn_y(i);
    int w = escmenu_btn_w;
    int h = escmenu_btn_h;
    if (escmenu_hov == i) {
        escmenu_draw_ring(fd, bx, by, w, h);
    }
    int label_color = escmenu_clr_text;
    if (escmenu_press == i) {
        escmenu_paint_rrect6(fd, bx, by, w, h, escmenu_clr_border);
        label_color = escmenu_clr_panel;
    } else {
        escmenu_paint_rrect6(fd, bx, by, w, h, escmenu_clr_panel);
        escmenu_frame_rrect6(fd, bx, by, w, h, escmenu_clr_border);
    }
    int tx = bx + (w - escmenu_label_w(i)) / 2;
    int ty = by + h / 2 - 7;
    if (i == 0) {
        draw_str(fd, tx, ty, label_color, 16, "Continue");
    } else {
        if (i == 1) {
            draw_str(fd, tx, ty, label_color, 16, "Restart App");
        } else {
            draw_str(fd, tx, ty, label_color, 16, "Quit");
        }
    }
    return 0;
}

/**
 * Draws the menu overlay if open (no-op otherwise). Call this last, after
 * the app's own frame content, so the panel sits on top. `fd` is the same
 * /dev/draw fd the app already uses for begin_frame/end_frame.
 */
int escmenu_draw(int fd) {
    if (!escmenu_open) {
        return 0;
    }
    int px = escmenu_panel_x();
    int py = escmenu_panel_y();
    fill_rect(fd, px - 2, py - 2, escmenu_panel_w + 4, escmenu_panel_h + 4, escmenu_clr_border);
    fill_rect(fd, px, py, escmenu_panel_w, escmenu_panel_h, escmenu_clr_panel);
    draw_str(fd, px + 35, py + 15, escmenu_clr_text, 16, "System Menu");
    escmenu_draw_btn(fd, 0);
    escmenu_draw_btn(fd, 1);
    escmenu_draw_btn(fd, 2);
    return 0;
}
