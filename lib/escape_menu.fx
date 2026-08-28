/* Generalized escape/pause menu for Fluxio apps.
 *
 * Pressing Esc opens a centered "Paused" panel with Resume/Quit buttons
 * on top of whatever the app already drew that frame; Resume (or Esc
 * again) closes it, Quit sets a flag the app's own main loop checks to
 * exit cleanly. Pure Fluxio -- fill_rect/draw_str only, no extern/ABI
 * link required (unlike apps/fluxio/Quill.fx's linked UI library) -- so
 * any app standardizes on the same pause menu with just:
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
 *
 * Esc (key code 27) toggles open/closed, matching the Esc-quits
 * convention apps/fluxio/Quill.fx already uses. escmenu_key/escmenu_mouse
 * both consume (return 1) everything while the menu is open, so a caller
 * gates its own input handling on the return value rather than checking
 * escmenu_is_open() at every call site.
 */

int escmenu_open;
int escmenu_quit;
int escmenu_canvas_w;
int escmenu_canvas_h;

int escmenu_panel_w = 220;
int escmenu_panel_h = 130;
int escmenu_btn_w = 160;
int escmenu_btn_h = 32;
int escmenu_btn_gap = 14;

int escmenu_clr_dim = 0x000000;
int escmenu_clr_panel = 0x303030;
int escmenu_clr_border = 0xAAAAAA;
int escmenu_clr_text = 0xFFFFFF;
int escmenu_clr_btn = 0x505050;

/** Resets and centers the menu for a canvas of size w x h -- call once, right after canvas_size(fd). */
int escmenu_init(int w, int h) {
    escmenu_canvas_w = w;
    escmenu_canvas_h = h;
    escmenu_open = 0;
    escmenu_quit = 0;
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

/** internal: left edge of both buttons (they share a common center) */
int escmenu_btn_x() {
    return escmenu_panel_x() + (escmenu_panel_w - escmenu_btn_w) / 2;
}

/** internal: top edge of button `i` (0 = Resume, 1 = Quit) */
int escmenu_btn_y(int i) {
    return escmenu_panel_y() + 40 + i * (escmenu_btn_h + escmenu_btn_gap);
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

/**
 * Feeds one keydown code through the menu. Esc (27) toggles open/closed
 * and is always consumed (the app shouldn't act on the Esc that just
 * opened or closed the menu either); every other key is consumed only
 * while the menu is open. Returns 1 if consumed, 0 if the caller should
 * handle the key itself.
 */
int escmenu_key(int key) {
    if (key == 27) {
        if (escmenu_open) {
            escmenu_open = 0;
        } else {
            escmenu_open = 1;
        }
        return 1;
    }
    if (escmenu_open) {
        return 1;
    }
    return 0;
}

/**
 * Feeds one mouse event (same mtype/mbutton/mx/my shape as poll_mouse()'s
 * mouse_type()/mouse_button()/mouse_x()/mouse_y() accessors) through the
 * menu. Consumes (returns 1) everything while open: a button-down
 * (mtype == 3) on Resume closes the menu, a button-down on Quit sets the
 * quit flag (escmenu_wants_quit()) and leaves the menu open -- the app's
 * main loop is expected to check the flag and exit before drawing
 * another frame. Returns 0 while closed.
 */
int escmenu_mouse(int mtype, int mbutton, int mx, int my) {
    if (!escmenu_open) {
        return 0;
    }
    if (mtype == 3) {
        if (escmenu_hit_btn(0, mx, my)) {
            escmenu_open = 0;
        } else {
            if (escmenu_hit_btn(1, mx, my)) {
                escmenu_quit = 1;
            }
        }
    }
    return 1;
}

/** True (1) once Quit has been clicked -- check in the main loop's exit condition. */
int escmenu_wants_quit() {
    return escmenu_quit;
}

/** True (1) while the menu is open -- callers can use this to pause their own game/animation logic. */
int escmenu_is_open() {
    return escmenu_open;
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
    fill_rect(fd, 0, 0, escmenu_canvas_w, escmenu_canvas_h, escmenu_clr_dim);

    int px = escmenu_panel_x();
    int py = escmenu_panel_y();
    fill_rect(fd, px, py, escmenu_panel_w, escmenu_panel_h, escmenu_clr_panel);
    fill_rect(fd, px, py, escmenu_panel_w, 1, escmenu_clr_border);
    fill_rect(fd, px, py + escmenu_panel_h - 1, escmenu_panel_w, 1, escmenu_clr_border);
    fill_rect(fd, px, py, 1, escmenu_panel_h, escmenu_clr_border);
    fill_rect(fd, px + escmenu_panel_w - 1, py, 1, escmenu_panel_h, escmenu_clr_border);
    draw_str(fd, px + 62, py + 12, escmenu_clr_text, 16, "Paused");

    int bx = escmenu_btn_x();
    fill_rect(fd, bx, escmenu_btn_y(0), escmenu_btn_w, escmenu_btn_h, escmenu_clr_btn);
    draw_str(fd, bx + 46, escmenu_btn_y(0) + 8, escmenu_clr_text, 16, "Resume");
    fill_rect(fd, bx, escmenu_btn_y(1), escmenu_btn_w, escmenu_btn_h, escmenu_clr_btn);
    draw_str(fd, bx + 62, escmenu_btn_y(1) + 8, escmenu_clr_text, 16, "Quit");
    return 0;
}
