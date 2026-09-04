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
 *     if (!escmenu_key(kbd_key())) { ... }      // app's own key handling,
 *                                                // skipped while the menu
 *                                                // is open or just toggled
 *     if (!escmenu_mouse(mtype, mbtn, mx, my)) { ... } // same for mouse
 *     ...
 *     escmenu_draw(fd);                         // last, after the app's
 *                                                // own frame content
 *     if (escmenu_wants_quit()) { break; }       // in the main loop's
 *                                                 // exit check
 *     if (escmenu_wants_restart()) { ...; escmenu_ack_restart(); }
 *
 * Esc (key code 27) toggles open/closed. While open, Up/Down move the
 * default ring and Enter fires the highlighted button. escmenu_key/
 * escmenu_mouse both consume (return 1) everything while the menu is
 * open, so a caller gates its own input handling on the return value.
 */

int escmenu_open;
int escmenu_quit;
int escmenu_restart;
int escmenu_hov;
int escmenu_canvas_w;
int escmenu_canvas_h;

/* Match lib/app.lux @draw-esc-popup / @esc-build: 200x160 sheet, 120x20
 * buttons at y+50 / y+78 / y+106. */
int escmenu_panel_w = 200;
int escmenu_panel_h = 160;
int escmenu_btn_w = 120;
int escmenu_btn_h = 20;
int escmenu_btn_gap = 8;

int escmenu_clr_panel = 0xFFFFFF;
int escmenu_clr_border = 0x000000;
int escmenu_clr_text = 0x000000;

/** Resets and centers the menu for a canvas of size w x h -- call once, right after canvas_size(fd). */
int escmenu_init(int w, int h) {
    escmenu_canvas_w = w;
    escmenu_canvas_h = h;
    escmenu_open = 0;
    escmenu_quit = 0;
    escmenu_restart = 0;
    escmenu_hov = 0;
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
 * Feeds one keydown code through the menu. Esc (27) toggles open/closed
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
            escmenu_hov = 0;
        }
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

/**
 * Feeds one mouse event (same mtype/mbutton/mx/my shape as poll_mouse()'s
 * mouse_type()/mouse_button()/mouse_x()/mouse_y() accessors) through the
 * menu. Consumes (returns 1) everything while open: a button-down
 * (mtype == 3) on Continue closes the menu, Restart App closes it and
 * sets escmenu_wants_restart(), Quit sets escmenu_wants_quit() and
 * leaves the menu open -- the app's main loop is expected to check that
 * flag and exit before drawing another frame. Returns 0 while closed.
 */
int escmenu_mouse(int mtype, int mbutton, int mx, int my) {
    if (!escmenu_open) {
        return 0;
    }
    if (mtype == 3) {
        if (escmenu_hit_btn(0, mx, my)) {
            escmenu_fire(0);
        } else {
            if (escmenu_hit_btn(1, mx, my)) {
                escmenu_fire(1);
            } else {
                if (escmenu_hit_btn(2, mx, my)) {
                    escmenu_fire(2);
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

/** internal: 1px rectangle outline */
int escmenu_stroke(int fd, int x, int y, int w, int h, int color) {
    fill_rect(fd, x, y, w, 1, color);
    fill_rect(fd, x, y + h - 1, w, 1, color);
    fill_rect(fd, x, y, 1, h, color);
    fill_rect(fd, x + w - 1, y, 1, h, color);
    return 0;
}

/** internal: System 6 push-button face; default ring when `i` is highlighted */
int escmenu_draw_btn(int fd, int i) {
    int bx = escmenu_btn_x();
    int by = escmenu_btn_y(i);
    int w = escmenu_btn_w;
    int h = escmenu_btn_h;
    if (escmenu_hov == i) {
        escmenu_stroke(fd, bx - 4, by - 4, w + 8, h + 8, escmenu_clr_border);
        escmenu_stroke(fd, bx - 3, by - 3, w + 6, h + 6, escmenu_clr_border);
        escmenu_stroke(fd, bx - 2, by - 2, w + 4, h + 4, escmenu_clr_border);
    }
    fill_rect(fd, bx, by, w, h, escmenu_clr_border);
    fill_rect(fd, bx + 1, by + 1, w - 2, h - 2, escmenu_clr_panel);
    if (i == 0) {
        draw_str(fd, bx + 22, by + 4, escmenu_clr_text, 16, "Continue");
    } else {
        if (i == 1) {
            draw_str(fd, bx + 10, by + 4, escmenu_clr_text, 16, "Restart App");
        } else {
            draw_str(fd, bx + 44, by + 4, escmenu_clr_text, 16, "Quit");
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
