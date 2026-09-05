/* Quill (Fluxio port, v13 -- word-wrap + mouse selection + hex view with
 * nibble editing + scrollbar (text and hex mode both) + status bar +
 * viewport auto-follow + menu bar with a live hex-mode checkbox + a real
 * File > Open... file picker + File > Save As + an Edit menu (Cut/Copy/Paste/Select All)
 * + File > New with an unsaved-changes confirm dialog) --
 * docs/quill_fluxio.md Phase C.
 *
 * Port of apps/Quill.lux's core edit loop: open a file (the fixed
 * scratch path at startup, or any file picked via File > Open...),
 * display it with word-wrap, move the cursor (keyboard or mouse click),
 * select text by dragging, cut/copy/paste it through the same /sys/snarf
 * clipboard file apps/Quill.lux uses, insert/delete characters, save,
 * start a fresh document via File > New (prompting to save first if
 * there are unsaved changes -- apps/Quill.lux's own menu-new never
 * prompts at all, so this is new behavior, not parity work), view and
 * edit it as hex, scroll with a real scrollbar (which follows the
 * cursor on its own past the edge of the pane), see filename/dirty/
 * row:col in a status bar, and use a real File/Edit/View menu bar
 * (feeds/draws/fires in both text and hex mode, with a checkmark
 * tracking the current mode) -- all via extern bindings into the Phase B
 * linked UI library (lib/uisf.bin) instead of being pure self-contained
 * Fluxio. Known simplifications vs Quill.lux, to be closed in a later
 * pass:
 *   - No shift-to-extend-selection from the keyboard -- Fluxio has no
 *     keyboard-modifier accessor builtin today, so selection is
 *     mouse-drag only (click sets the anchor, drag extends, release
 *     keeps it). Arrow keys collapse an active selection instead.
 *   - Hex-mode editing is nibble-only, overwrite-in-place (type a hex
 *     digit to set the nibble under the cursor, auto-advancing after the
 *     low nibble) -- it can't insert/delete bytes or extend the file,
 *     unlike Quill.lux's text-mode editing. Cut/Copy/Paste are text-mode
 *     only for the same reason (there's no selection concept in hex
 *     mode to act on).
 *   - Cut/Copy/Paste/Select All are menu-only, no keyboard shortcuts --
 *     same reason Save/Toggle Hex use Tab/Home placeholders instead of
 *     real Ctrl+X/C/V: Fluxio has no keyboard-modifier accessor builtin,
 *     and unlike Tab/Home, 'x'/'c'/'v' are ordinary typable characters
 *     that can't be repurposed as bare-key shortcuts without breaking
 *     normal typing.
 *   - Save is still also bound to Tab, and hex-mode-toggle to Home, as
 *     the pre-menu placeholders -- kept since Fluxio still has no
 *     keyboard-modifier builtin for a real Ctrl+S.
 *   - 64KB buffer cap, not Quill.lux's 1MB + lazy chunked load/save.
 *   - Up to 4096 wrapped lines are cached; a file that wraps to more
 *     lines than that renders/clicks correctly up to the cap and then
 *     stops scanning further lines for this frame (silent, like
 *     Quill.lux's own documented LINE_STARTS headroom assumption) --
 *     not a concern at the current 64KB buffer cap in practice.
 *
 * make all
 * ./bin/cloister   (pick Quill.bin from the Fluxio column)
 */

/* Esc opens the same System 6 "System Menu" sheet every other Fluxio app
 * gets. Before this, Esc quit Quill outright -- silently discarding
 * unsaved edits, with no confirmation and no way back -- which is also
 * why the Esc menu appeared to work only in Snake. */
include "../../lib/escape_menu.fx";

/* Trampoline slot addresses are abi/uisf.exports.json's committed,
 * append-only order: MM_ABI_LIBRARY_LINK_BASE (0x700000) + 12 +
 * 5*index. Built and linked by the apps/fluxio/Quill.bin Makefile rule
 * (luxc -base 0x701000 for the library, then fluxlink --lib-base
 * 0x700000 to merge it with this file's own compiled output). See
 * docs/quill_fluxio.md Phase B7. */
version 399000;

extern void ui_new() = 0x70000C;                                                    /* index 0 */
extern void ui_sbar_init(int addr, int x, int y, int w, int h, int min, int max, int val, int horiz) = 0x70002A; /* index 6 */
extern void ui_sbar_draw(int fd, int addr) = 0x70002F;                              /* index 7 */
extern int ui_sbar_press(int addr, int mx, int my) = 0x700034;                      /* index 8 */
extern int ui_sbar_drag(int addr, int mx, int my) = 0x700039;                       /* index 9 */
extern void ui_sbar_release(int addr) = 0x70003E;                                   /* index 10 */
extern int ui_in_rect(int mx, int my, int x, int y, int w, int h) = 0x700070;       /* index 20 */
extern int ui_sbar_set_val(int addr, int val) = 0x700043;                          /* index 11 */
extern void ui_menubar(int w) = 0x700011;                                           /* index 1 */
extern void ui_menu(int title) = 0x700016;                                          /* index 2 */
extern void ui_item(int text, int handler) = 0x70001B;                              /* index 3 */
extern void ui_check_item(int text, int handler) = 0x700020;                       /* index 4 */
extern void ui_item_set(int name, int val) = 0x7000AC;                             /* index 32 */
extern void ui_feed(int mpkt) = 0x700048;                                           /* index 12 */
extern void ui_draw() = 0x70004D;                                                   /* index 13 */
extern int ui_menu_open() = 0x700057;                                               /* index 15 */
extern int ui_poll_next() = 0x70005C;                                               /* index 16 */
extern int ui_poll_name() = 0x700061;                                               /* index 17 */
extern void draw_use(int fd) = 0x7000A7;                                            /* index 31 */
extern void sf_show() = 0x700075;                                                   /* index 21 */
extern int sf_is_open() = 0x70007F;                                                 /* index 23 */
extern void sf_mouse(int mpkt) = 0x700089;                                          /* index 25 */
extern void sf_kbd(int kpkt) = 0x70008E;                                            /* index 26 */
extern void sf_draw() = 0x700093;                                                   /* index 27 */
extern int sf_picked() = 0x700098;                                                  /* index 28 */
extern int sf_cancelled() = 0x70009D;                                               /* index 29 */
extern void sf_clear_result() = 0x7000A2;                                           /* index 30 */
extern int sf_path_copy(int dest, int max) = 0x7000B1;                              /* index 33 */
extern void app_win_set(int w, int h) = 0x7000B6;                                   /* index 34 */
extern void ui_radio_item(int text, int group, int handler) = 0x7000BB;             /* index 35 */
extern void sf_show_save(int name) = 0x7000C0;                                      /* index 36 */

byte file_buf[65536];
byte font_widths[256];
byte font_cmd[4];
byte hex_line_buf[72];
byte font_grp[8] = "font";
byte menu_font_label[8] = "Font";
byte menu_chicago_label[8] = "Chicago";
byte menu_geneva_label[8] = "Geneva";
byte menu_monaco_label[8] = "Monaco";
byte scratch_path[300] = "/sys/file/quill_scratch.txt";
int scratch_path_len = 28;
/* The path Quill opens on launch, kept so the Esc menu's "Restart App"
 * can get back to the startup document even after File > Open/New has
 * repointed scratch_path somewhere else. lib/app.lux's own esc-restart
 * does this by jumping to the program entry (0x600000 JMPSTACK); Fluxio
 * has no equivalent, so restart_app() re-runs the init by hand. */
byte startup_path[300] = "/sys/file/quill_scratch.txt";
int startup_path_len = 28;
byte sf_raw_path[256];
byte vfs_file_prefix[12] = "/sys/file";
byte sf_kpkt[8];
byte sb_bar[56];
byte status_label[64] = "quill_scratch.txt";
int status_label_len = 17;
byte status_buf[64];
byte ui_mpkt[8];
byte menu_file_label[8] = "File";
byte menu_new_label[8] = "New";
byte menu_open_label[8] = "Open";
byte menu_save_label[8] = "Save";
byte menu_saveas_label[12] = "Save As";
byte menu_quit_label[8] = "Quit";
byte menu_view_label[8] = "View";
byte menu_hex_label[16] = "Toggle Hex";
byte menu_edit_label[8] = "Edit";
byte menu_cut_label[8] = "Cut";
byte menu_copy_label[8] = "Copy";
byte menu_paste_label[8] = "Paste";
byte menu_selectall_label[16] = "Select All";
byte paste_buf[65536];
byte new_doc_name[12] = "/new.quill";

/* File > New's "unsaved changes?" confirm dialog -- a small self-
 * contained overlay (fill_rect/draw_str only, no linked-library extern
 * needed) in the same style as the panel/button look, distinct from the
 * SF file-picker modal (lib/sf.lux) which is a separate, linked-library
 * dialog for a different purpose. */
int confirm_new_open;
int save_as;
int confirm_panel_w = 260;
int confirm_panel_h = 170;
int confirm_btn_w = 180;
int confirm_btn_h = 30;
int confirm_btn_gap = 10;
int clr_confirm_dim = 0x000000;
int clr_confirm_panel = 0x303030;
int clr_confirm_border = 0xAAAAAA;
int clr_confirm_text = 0xFFFFFF;
int clr_confirm_btn = 0x505050;
int file_len;
int cursor;
int anchor = -1;
int mouse_held;
int hex_mode;
int hex_nibble;
int app_font;
int last_cursor = -1;
int dirty;
int sb_dragging;
int sb_x;
int sb_y;
int sb_w = 16;
int sb_h;

int line_starts[4096];
int line_count;

int canvas_w;
int canvas_h;

int pane_x = 16;
int pane_y = 40;
int line_h = 20;
int font_size = 18;
int caret_h = 14;
int status_h = 24;
int clr_bg = 0xFFFFFF;
int clr_text = 0x000000;
int clr_caret = 0xCC0000;
int clr_hex_caret = 0x0000FF;
int clr_sel = 0xCCDDFF;
int clr_frame = 0xAAAAAA;
int clr_status_bg = 0xEEEEEE;

/**
 * Pixel advance of one glyph at font_size, from the document face
 * (Font menu; /sys/font/widths). Menus stay Chicago. 16px-nominal
 * widths scaled to font_size match the renderer. Newline has no width.
 * A zero table entry falls back to 6px, same as Quill.lux.
 */
int ch_advance(int ch) {
    if (ch == 10) {
        return 0;
    }
    int w = font_widths[ch];
    if (w == 0) {
        w = 6;
    }
    return w * font_size / 16;
}

/**
 * Shifts file_buf[from..file_len) right by n bytes (opens a gap for an
 * insert at `from`). Caller is responsible for file_len already
 * reflecting the post-insert size and for n not overrunning the buffer.
 */
int shift_right(int from, int n) {
    int i = file_len - 1;
    while (i >= from) {
        file_buf[i + n] = file_buf[i];
        i = i - 1;
    }
    return 0;
}

/**
 * Shifts file_buf[from..file_len) left by n bytes (closes a gap after a
 * delete). Caller updates file_len separately.
 */
int shift_left(int from, int n) {
    int i = from;
    while (i < file_len) {
        file_buf[i - n] = file_buf[i];
        i = i + 1;
    }
    return 0;
}

/**
 * Rebuilds the word-wrapped line-start cache for the current canvas_w.
 * Same idea as Quill.lux's rebuild-lines, but wrapping on pixel width
 * (via ch_advance) instead of a fixed column count, since Fluxio draws
 * with the same proportional font Quill.lux measures against. Wraps at
 * the last space seen on the current line when one exists past the line's
 * own start; otherwise falls back to a hard char-wrap (a single word or
 * character wider than the pane can't wrap at a space that doesn't
 * exist). Must run after every edit and after loading, before the next
 * draw/click-mapping pass.
 */
int rebuild_lines() {
    int max_x = canvas_w - 16;
    int n = 1;
    line_starts[0] = 0;
    int line_start = 0;
    int col_x = pane_x;
    int last_space = -1;
    int i = 0;
    while (i < file_len) {
        int ch = file_buf[i];
        if (ch == 10) {
            i = i + 1;
            line_start = i;
            col_x = pane_x;
            last_space = -1;
            if (n < 4096) {
                line_starts[n] = line_start;
                n = n + 1;
            }
        } else {
            int adv = ch_advance(ch);
            if (col_x > pane_x) {
                if (col_x + adv > max_x) {
                    if (last_space > line_start) {
                        line_start = last_space + 1;
                    } else {
                        line_start = i;
                    }
                    col_x = pane_x;
                    last_space = -1;
                    if (n < 4096) {
                        line_starts[n] = line_start;
                        n = n + 1;
                    }
                    i = line_start;
                } else {
                    if (ch == 32) {
                        last_space = i;
                    }
                    col_x = col_x + adv;
                    i = i + 1;
                }
            } else {
                if (ch == 32) {
                    last_space = i;
                }
                col_x = col_x + adv;
                i = i + 1;
            }
        }
    }
    line_count = n;
    update_scrollbar_geometry();
    return 0;
}

/**
 * Reads the scrollbar's current value (BAR_VAL, byte offset 24 of the
 * 56-byte UI::sbar struct) straight out of sb_bar -- the VM stores words
 * big-endian, so this reconstructs the value from 4 individually-read
 * bytes rather than needing a dedicated getter export. sb_bar's own
 * memory is the single source of truth for the current scroll offset;
 * nothing here mirrors it into a separate Fluxio-side variable.
 */
int sb_get_val() {
    int b0 = sb_bar[24];
    int b1 = sb_bar[25];
    int b2 = sb_bar[26];
    int b3 = sb_bar[27];
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/**
 * How many text rows fit in the pane between the header and the status
 * bar at the bottom.
 */
int visible_lines() {
    return (canvas_h - pane_y - status_h) / line_h;
}

/**
 * How many 16-byte hex-dump rows the buffer takes, including the
 * trailing (possibly empty) row at end-of-buffer draw_hex_buffer also
 * draws -- same row count that loop produces.
 */
int hex_row_count() {
    return file_len / 16 + 1;
}

/**
 * Highest legal scroll offset (in rows) for the current content, canvas
 * size, and mode -- 0 once everything fits on screen. Text mode counts
 * wrapped lines; hex mode counts 16-byte rows -- different units, same
 * `sb_bar` widget, re-ranged by update_scrollbar_geometry whenever the
 * mode changes.
 */
int max_scroll() {
    int rows = line_count;
    if (hex_mode) {
        rows = hex_row_count();
    }
    int m = rows - visible_lines();
    if (m < 0) {
        m = 0;
    }
    return m;
}

/**
 * (Re)initializes the linked scrollbar's geometry and range from the
 * current canvas size and line count, preserving (clamped) whatever
 * scroll position it already had. Called after every rebuild_lines --
 * i.e. after every edit and after load -- since the line count (and so
 * the valid scroll range) can change on every keystroke.
 */
int update_scrollbar_geometry() {
    int old_val = sb_get_val();
    sb_x = canvas_w - sb_w;
    sb_y = pane_y;
    sb_h = canvas_h - pane_y - status_h;
    int m = max_scroll();
    if (old_val > m) {
        old_val = m;
    }
    if (old_val < 0) {
        old_val = 0;
    }
    ui_sbar_init(sb_bar, sb_x, sb_y, sb_w, sb_h, 0, m, old_val, 0);
    return 0;
}

/**
 * Flips hex_mode and re-ranges the scrollbar for the new mode's row
 * units (text: wrapped lines; hex: 16-byte rows) -- the two modes share
 * one `sb_bar` widget, so switching modes without this would leave its
 * min/max describing the mode you just left. The old scroll value is
 * clamped into the new range by update_scrollbar_geometry, not reset;
 * it won't line up with anything meaningful across the switch, but
 * clamping is a reasonable fallback and avoids always snapping to the
 * top.
 *
 * Also syncs the View > Toggle Hex menu item's checkmark via
 * UI::item-set, explicitly, rather than relying on UI::menu's own
 * auto-toggle-on-click behavior for check-items: this function is called
 * from two places (the menu click itself, and the Home-key shortcut), and
 * only the menu's own internal click handling would auto-flip the
 * checkmark -- the Home-key path wouldn't touch it at all, leaving the
 * checkbox showing the wrong state as soon as someone used the shortcut
 * instead of the menu.
 */
int toggle_hex_mode() {
    if (hex_mode) {
        hex_mode = 0;
    } else {
        hex_mode = 1;
    }
    hex_nibble = 0;
    anchor = -1;
    mouse_held = 0;
    update_scrollbar_geometry();
    ui_item_set(menu_hex_label, hex_mode);
    return 0;
}

/**
 * Scrolls the view (via UI::sbar-set-val) so the cursor's wrapped line
 * is within the visible range, if it isn't already -- keeps typing and
 * arrow-key/mouse cursor movement from running the cursor off the
 * bottom (or top) of the pane without the view following it. Called
 * from main() only on frames where `cursor` actually changed since the
 * last one (see `last_cursor`), not every frame -- otherwise a manual
 * scrollbar drag that doesn't move the cursor would get snapped straight
 * back to the cursor's line on the very next frame, since nothing else
 * would distinguish "the user scrolled" from "the cursor moved".
 */
int ensure_cursor_visible() {
    int row = cursor_row();
    if (hex_mode) {
        row = cursor / 16;
    }
    int scroll_y = sb_get_val();
    int vis = visible_lines();
    if (row < scroll_y) {
        ui_sbar_set_val(sb_bar, row);
    } else {
        if (row >= scroll_y + vis) {
            ui_sbar_set_val(sb_bar, row - vis + 1);
        }
    }
    return 0;
}

/**
 * Pixel x of file_buf[idx] within a line starting at `start` (idx must be
 * >= start), by walking the glyphs in between and summing their advances.
 */
int line_x_at(int start, int idx) {
    int x = pane_x;
    int i = start;
    while (i < idx) {
        x = x + ch_advance(file_buf[i]);
        i = i + 1;
    }
    return x;
}

/**
 * Maps a click/drag point to the nearest byte index, using the line
 * cache built by rebuild_lines and the scrollbar's current offset (only
 * on-screen lines are considered -- a point over a scrolled-off line
 * simply doesn't match). A point below all visible lines maps to
 * end-of-buffer; a point past a line's last glyph maps to that line's
 * end (its trailing newline if it has one, else file_len).
 */
int find_click_index(int mx, int my) {
    int scroll_y = sb_get_val();
    int vis = visible_lines();
    int li = 0;
    while (li < line_count) {
        int start = line_starts[li];
        int end = file_len;
        if (li + 1 < line_count) {
            end = line_starts[li + 1];
        }
        int screen_row = li - scroll_y;
        if (screen_row >= 0) {
            if (screen_row < vis) {
                int y = pane_y + screen_row * line_h;
                if (my < y + line_h) {
                    int x = pane_x;
                    int i = start;
                    while (i < end) {
                        int ch = file_buf[i];
                        if (ch == 10) {
                            return i;
                        }
                        int adv = ch_advance(ch);
                        if (mx < x + adv / 2) {
                            return i;
                        }
                        x = x + adv;
                        i = i + 1;
                    }
                    if (end > start) {
                        if (file_buf[end - 1] == 10) {
                            return end - 1;
                        }
                    }
                    return end;
                }
            }
        }
        li = li + 1;
    }
    return file_len;
}

/**
 * Deletes the active selection (anchor..cursor, either order) if one
 * exists, collapsing the cursor to its start and clearing the anchor.
 * A no-op if there's no active selection.
 */
int delete_selection() {
    if (anchor < 0) {
        return 0;
    }
    if (anchor == cursor) {
        return 0;
    }
    int lo = anchor;
    int hi = cursor;
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    shift_left(hi, hi - lo);
    file_len = file_len - (hi - lo);
    cursor = lo;
    anchor = -1;
    dirty = 1;
    rebuild_lines();
    return 0;
}

/**
 * Inserts one byte at the cursor and advances it, if there's room.
 */
int insert_char(int ch) {
    if (file_len >= 65536) {
        return 0;
    }
    file_len = file_len + 1;
    shift_right(cursor, 1);
    file_buf[cursor] = ch;
    cursor = cursor + 1;
    dirty = 1;
    rebuild_lines();
    return 0;
}

/**
 * Deletes the byte just before the cursor, if any.
 */
int delete_char() {
    if (cursor <= 0) {
        return 0;
    }
    shift_left(cursor, 1);
    file_len = file_len - 1;
    cursor = cursor - 1;
    dirty = 1;
    rebuild_lines();
    return 0;
}

/**
 * True (1) if a non-empty selection is active (anchor set and different
 * from cursor) -- same "has_sel" test main()'s keyboard handler computes
 * inline for Backspace/typing, factored out here for the Edit menu's
 * Cut/Copy actions below.
 */
int has_selection() {
    if (anchor < 0) {
        return 0;
    }
    if (anchor == cursor) {
        return 0;
    }
    return 1;
}

/**
 * Writes the active selection's bytes to the /sys/snarf clipboard file
 * -- the same mechanism apps/Quill.lux's snarf-selection uses, so Cut/
 * Copy here interoperate with it. Doesn't touch the buffer: Copy calls
 * this alone, Cut calls this then delete_selection(). A no-op if there's
 * no selection or the clipboard can't be opened.
 */
int copy_selection() {
    if (!has_selection()) {
        return 0;
    }
    int lo = anchor;
    int hi = cursor;
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    int sfd = vfs_open("/sys/snarf");
    if (sfd < 0) {
        return 0;
    }
    vfs_write(sfd, file_buf + lo, hi - lo);
    vfs_close(sfd);
    return 0;
}

/**
 * Inserts `n` bytes from `src` at the cursor and advances it, if there's
 * room -- the multi-byte counterpart to insert_char(), used by
 * paste_snarf(). A no-op (matching apps/Quill.lux's type-bytes) rather
 * than a truncated partial insert if it wouldn't fit.
 */
int insert_bytes(byte src[], int n) {
    if (file_len + n > 65536) {
        return 0;
    }
    shift_right(cursor, n);
    int i = 0;
    while (i < n) {
        file_buf[cursor + i] = src[i];
        i = i + 1;
    }
    file_len = file_len + n;
    cursor = cursor + n;
    dirty = 1;
    rebuild_lines();
    return 0;
}

/**
 * Reads the /sys/snarf clipboard file and inserts it at the cursor,
 * replacing the active selection first if there is one (same as typing
 * a character over a selection) -- text mode only, matching
 * apps/Quill.lux's paste-snarf (there's no selection concept in hex mode
 * to replace). A no-op if the clipboard is empty, unreadable, or the
 * paste wouldn't fit.
 */
int paste_snarf() {
    if (hex_mode) {
        return 0;
    }
    int sfd = vfs_open("/sys/snarf");
    if (sfd < 0) {
        return 0;
    }
    int n = vfs_read(sfd, paste_buf, 65536);
    vfs_close(sfd);
    if (n <= 0) {
        return 0;
    }
    if (has_selection()) {
        delete_selection();
    }
    insert_bytes(paste_buf, n);
    return 0;
}

/**
 * Selects the entire buffer (anchor at 0, cursor at file_len) -- same as
 * apps/Quill.lux's select-all. A no-op on an empty buffer.
 */
int select_all() {
    if (file_len > 0) {
        anchor = 0;
        cursor = file_len;
    }
    return 0;
}

/**
 * Edits one nibble of the byte at `cursor` in place (hex mode only;
 * caller has already checked cursor < file_len -- there's no byte there
 * to edit at end-of-file, and this doesn't grow the buffer). `hex_nibble`
 * selects which half: 0 is the high nibble (edited first), 1 the low.
 * After the high nibble, hex_nibble advances to the low nibble of the
 * same byte; after the low nibble, it wraps back to 0 and the cursor
 * advances to the next byte (unless already on the last one, where it
 * just stays put so the same byte can keep being edited) -- the usual
 * "two keystrokes per byte, auto-advance" hex-editor typing rhythm.
 * rebuild_lines() keeps the word-wrap cache from going stale for when
 * the user flips back to text mode after editing bytes here.
 */
int edit_hex_nibble(int digit) {
    int b = file_buf[cursor];
    if (hex_nibble == 0) {
        b = (b & 0x0F) | (digit << 4);
    } else {
        b = (b & 0xF0) | digit;
    }
    file_buf[cursor] = b;
    dirty = 1;
    if (hex_nibble == 0) {
        hex_nibble = 1;
    } else {
        hex_nibble = 0;
        if (cursor + 1 < file_len) {
            cursor = cursor + 1;
        }
    }
    rebuild_lines();
    return 0;
}

/**
 * DRAW cmd 5: select the system face. 0 Chicago, 2 Geneva, 3 Monaco.
 */
int set_font(int fd, int which) {
    font_cmd[0] = 5;
    font_cmd[1] = which;
    vfs_write(fd, font_cmd, 2);
    return 0;
}

/**
 * Loads /sys/font/widths (the face last selected with set_font) into
 * font_widths. Missing files leave the table zeroed -- every advance
 * then falls back to 6px rather than failing the program.
 */
int load_font_widths() {
    int fd = vfs_open("/sys/font/widths");
    if (fd >= 0) {
        vfs_read(fd, font_widths, 256);
        vfs_close(fd);
    }
    return 0;
}

/**
 * Document face only. Reloads wrap metrics and Font menu radios.
 * Does not change menu/button drawing (those stay Chicago).
 */
int apply_text_font(int fd, int which) {
    app_font = which;
    set_font(fd, which);
    load_font_widths();
    ui_item_set(menu_chicago_label, 0);
    ui_item_set(menu_geneva_label, 0);
    ui_item_set(menu_monaco_label, 0);
    if (which == 0) {
        ui_item_set(menu_chicago_label, 1);
    } else {
        if (which == 2) {
            ui_item_set(menu_geneva_label, 1);
        } else {
            ui_item_set(menu_monaco_label, 1);
        }
    }
    rebuild_lines();
    return 0;
}

/**
 * Loads the scratch file into file_buf. Tries read+write first (an
 * existing file); if that fails (file doesn't exist yet), creates it
 * empty instead -- vfs_open_buf/create_host_file has no single flag
 * combination for "open existing or create, without truncating either
 * way" today, so this is the two-step workaround.
 */
int load_file() {
    int fd = vfs_open_buf(scratch_path, scratch_path_len, 2);
    if (fd < 0) {
        fd = vfs_open_buf(scratch_path, scratch_path_len, 6);
        file_len = 0;
        cursor = 0;
        dirty = 0;
        if (fd >= 0) {
            vfs_close(fd);
        }
        rebuild_lines();
        return 0;
    }
    file_len = vfs_read(fd, file_buf, 65536);
    if (file_len < 0) {
        file_len = 0;
    }
    cursor = 0;
    dirty = 0;
    vfs_close(fd);
    rebuild_lines();
    return 0;
}

/**
 * Writes file_buf back to the scratch file (truncating to file_len).
 */
int save_file() {
    int fd = vfs_open_buf(scratch_path, scratch_path_len, 6);
    if (fd < 0) {
        return 0;
    }
    vfs_write(fd, file_buf, file_len);
    vfs_close(fd);
    dirty = 0;
    return 0;
}

/**
 * Recomputes status_label (the filename shown in the status bar) from
 * scratch_path's current content -- the part after the last '/', so a
 * picked file deep in a directory still shows a short name instead of
 * its whole path. Capped well under status_buf's 64-byte capacity, which
 * also has to fit the " *" dirty marker and the row:col digits appended
 * after it (draw_status_line).
 */
int update_status_label_from_path() {
    int start = 0;
    int i = 0;
    while (i < scratch_path_len) {
        if (scratch_path[i] == 47) {
            start = i + 1;
        }
        i = i + 1;
    }
    int len = scratch_path_len - start;
    if (len > 40) {
        len = 40;
    }
    int j = 0;
    while (j < len) {
        status_label[j] = scratch_path[start + j];
        j = j + 1;
    }
    status_label_len = len;
    return 0;
}

/**
 * Loads the file the SF picker just chose: copies its picked path (a
 * virtual path relative to the picker's own root, e.g. "/notes.txt" or
 * "/sub/notes.txt" -- see lib/sf.lux's choose-file) into sf_raw_path via
 * SF::path-copy (the only way to read it: Fluxio can't peek an arbitrary
 * Lux-side pointer directly, see path-copy's own doc comment in
 * lib/sf.lux), prefixes it with "/sys/file" to get a real VFS path (the
 * same prefix apps/Quill.lux's make-vfs-path applies), and reuses
 * load_file() to actually open it -- same as the fixed-path startup load,
 * just against a scratch_path that now points somewhere else.
 */
/**
 * Copies the SF picker's chosen virtual path into scratch_path, prefixed
 * with /sys/file, and refreshes the status-bar basename. Shared by Open
 * (then load) and Save As (then save).
 */
int retarget_from_picker() {
    int raw_len = sf_path_copy(sf_raw_path, 255);
    int i = 0;
    while (i < 9) {
        scratch_path[i] = vfs_file_prefix[i];
        i = i + 1;
    }
    i = 0;
    while (i < raw_len) {
        scratch_path[9 + i] = sf_raw_path[i];
        i = i + 1;
    }
    scratch_path_len = 9 + raw_len;
    update_status_label_from_path();
    return 0;
}

/**
 * Loads the file the SF picker just chose: retargets scratch_path from
 * SF::path-copy, then reuses load_file().
 */
int open_picked_file() {
    retarget_from_picker();
    load_file();
    return 0;
}

/**
 * File > Save As: retargets scratch_path from the put-file picker's
 * chosen path, then writes the current buffer there. Does not load.
 */
int save_picked_file() {
    retarget_from_picker();
    save_file();
    return 0;
}

/**
 * The Esc menu's "Restart App": puts Quill back in its launch state --
 * startup document reloaded from disk, text mode, Chicago face, no
 * selection, no open modal, view scrolled to the top. Unsaved edits are
 * discarded, same as lib/app.lux's esc-restart re-entry does for Lux
 * apps.
 */
int restart_app(int fd) {
    int i = 0;
    while (i < startup_path_len) {
        scratch_path[i] = startup_path[i];
        i = i + 1;
    }
    scratch_path_len = startup_path_len;

    confirm_new_open = 0;
    save_as = 0;
    anchor = -1;
    mouse_held = 0;
    sb_dragging = 0;
    hex_nibble = 0;
    last_cursor = -1;
    if (hex_mode) {
        hex_mode = 0;
        ui_item_set(menu_hex_label, 0);
    }
    apply_text_font(fd, 0);

    load_file();
    update_status_label_from_path();
    update_scrollbar_geometry();
    ui_sbar_set_val(sb_bar, 0);
    return 0;
}

/**
 * File > New: discards the in-memory buffer and starts a fresh, empty
 * "new.quill" document -- same reset apps/Quill.lux's menu-new does (it
 * doesn't touch hex_mode either, so New in hex mode just clears the
 * buffer and stays in hex mode). There's no file fd to close first --
 * unlike Quill.lux, load_file()/save_file() here open and close the
 * scratch file each time rather than keeping one open across the whole
 * run, so "closing the current doc" is just resetting this state.
 * Callers are responsible for saving first if that's wanted (see
 * confirm_new_mouse() below, which is the only caller once there's
 * something to lose) -- this function always discards unconditionally.
 */
int new_document() {
    int i = 0;
    while (i < 9) {
        scratch_path[i] = vfs_file_prefix[i];
        i = i + 1;
    }
    i = 0;
    while (i < 10) {
        scratch_path[9 + i] = new_doc_name[i];
        i = i + 1;
    }
    scratch_path_len = 19;
    file_len = 0;
    cursor = 0;
    anchor = -1;
    dirty = 0;
    last_cursor = -1;
    update_status_label_from_path();
    rebuild_lines();
    update_scrollbar_geometry();
    return 0;
}

/**
 * File > New's confirm dialog: "Save changes?" (only opened when
 * `dirty` is set -- see the ui_poll_next() dispatch in main()). A no-op
 * discard-free File > New skips this whole dialog entirely.
 */

/** internal: left edge of the centered confirm panel */
int confirm_panel_x() {
    return (canvas_w - confirm_panel_w) / 2;
}

/** internal: top edge of the centered confirm panel */
int confirm_panel_y() {
    return (canvas_h - confirm_panel_h) / 2;
}

/** internal: left edge of all three stacked buttons (they share a common center) */
int confirm_btn_x() {
    return confirm_panel_x() + (confirm_panel_w - confirm_btn_w) / 2;
}

/** internal: top edge of button `i` (0 = Save, 1 = Don't Save, 2 = Cancel) */
int confirm_btn_y(int i) {
    return confirm_panel_y() + 46 + i * (confirm_btn_h + confirm_btn_gap);
}

/** internal: true (1) if (px, py) falls within button `i`'s rect */
int confirm_hit_btn(int i, int px, int py) {
    int bx = confirm_btn_x();
    int by = confirm_btn_y(i);
    if (px < bx) { return 0; }
    if (px >= bx + confirm_btn_w) { return 0; }
    if (py < by) { return 0; }
    if (py >= by + confirm_btn_h) { return 0; }
    return 1;
}

/**
 * Feeds one mouse event through the confirm dialog. Always consumed
 * (returns 1) while open -- same "modal owns all input" precedence as
 * the SF file picker (sf_is_open() in main()). Save writes the current
 * document first, then both Save and Don't Save discard it via
 * new_document(); Cancel just closes the dialog, leaving the current
 * document untouched.
 */
int confirm_new_mouse(int mtype, int mbutton, int mx, int my) {
    if (mtype == 3) {
        if (confirm_hit_btn(0, mx, my)) {
            save_file();
            confirm_new_open = 0;
            new_document();
        } else {
            if (confirm_hit_btn(1, mx, my)) {
                confirm_new_open = 0;
                new_document();
            } else {
                if (confirm_hit_btn(2, mx, my)) {
                    confirm_new_open = 0;
                }
            }
        }
    }
    return 1;
}

/**
 * Feeds one keydown code through the confirm dialog. Esc cancels (same
 * as the SF picker's own Esc-cancels behavior); every other key is
 * still consumed while open, same reasoning as escape_menu.fx's
 * escmenu_key().
 */
int confirm_new_kbd(int key) {
    if (key == 27) {
        confirm_new_open = 0;
    }
    return 1;
}

/**
 * Draws the confirm dialog if open (no-op otherwise). Called last, after
 * everything else including ui_draw()/sf_draw(), so it sits on top.
 */
int confirm_new_draw(int fd) {
    if (!confirm_new_open) {
        return 0;
    }
    fill_rect(fd, 0, 0, canvas_w, canvas_h, clr_confirm_dim);

    int px = confirm_panel_x();
    int py = confirm_panel_y();
    fill_rect(fd, px, py, confirm_panel_w, confirm_panel_h, clr_confirm_panel);
    fill_rect(fd, px, py, confirm_panel_w, 1, clr_confirm_border);
    fill_rect(fd, px, py + confirm_panel_h - 1, confirm_panel_w, 1, clr_confirm_border);
    fill_rect(fd, px, py, 1, confirm_panel_h, clr_confirm_border);
    fill_rect(fd, px + confirm_panel_w - 1, py, 1, confirm_panel_h, clr_confirm_border);
    draw_str(fd, px + 26, py + 16, clr_confirm_text, 16, "Save changes?");

    int bx = confirm_btn_x();
    fill_rect(fd, bx, confirm_btn_y(0), confirm_btn_w, confirm_btn_h, clr_confirm_btn);
    draw_str(fd, bx + 68, confirm_btn_y(0) + 9, clr_confirm_text, 16, "Save");
    fill_rect(fd, bx, confirm_btn_y(1), confirm_btn_w, confirm_btn_h, clr_confirm_btn);
    draw_str(fd, bx + 34, confirm_btn_y(1) + 9, clr_confirm_text, 16, "Don't Save");
    fill_rect(fd, bx, confirm_btn_y(2), confirm_btn_w, confirm_btn_h, clr_confirm_btn);
    draw_str(fd, bx + 52, confirm_btn_y(2) + 9, clr_confirm_text, 16, "Cancel");
    return 0;
}

/**
 * Copies `len` bytes from a global byte[] into status_buf starting at
 * `pos`. Used to assemble the status line piecewise (label, digits,
 * literal punctuation) into one buffer before drawing it in one call.
 */
int status_copy(int pos, byte src[], int len) {
    int i = 0;
    while (i < len) {
        status_buf[pos + i] = src[i];
        i = i + 1;
    }
    return pos + len;
}

/**
 * Writes the decimal digits of a non-negative int into status_buf at
 * `pos` and returns the position just past them. `digits` is a local
 * array only ever indexed here, never passed as a value, so it doesn't
 * need a stable address (see the local-array-as-value restriction noted
 * throughout this file's helper functions).
 */
int status_write_int(int n, int pos) {
    if (n == 0) {
        status_buf[pos] = 48;
        return pos + 1;
    }
    int digits[12];
    int nd = 0;
    int v = n;
    while (v > 0) {
        digits[nd] = v % 10;
        v = v / 10;
        nd = nd + 1;
    }
    int i = nd - 1;
    while (i >= 0) {
        status_buf[pos] = digits[i] + 48;
        pos = pos + 1;
        i = i - 1;
    }
    return pos;
}

/**
 * Index of the wrapped line (into line_starts) containing the cursor.
 */
int cursor_row() {
    int li = 0;
    while (li < line_count) {
        int start = line_starts[li];
        int end = file_len;
        if (li + 1 < line_count) {
            end = line_starts[li + 1];
        }
        if (cursor >= start) {
            if (cursor <= end) {
                return li;
            }
        }
        li = li + 1;
    }
    return 0;
}

/**
 * Draws the status bar: filename, a dirty marker if unsaved, and the
 * cursor's 1-indexed row:column -- same information as Quill.lux's
 * draw-status-line, assembled into status_buf and drawn in one
 * draw_bytes call. Always drawn last (main() calls this after
 * draw_buffer/draw_hex_buffer) so it sits on top of the pane content.
 */
int draw_status_line(int fd) {
    int y = canvas_h - status_h;
    fill_rect(fd, 0, y, canvas_w, status_h, clr_status_bg);
    fill_rect(fd, 0, y, canvas_w, 1, clr_frame);

    int pos = status_copy(0, status_label, status_label_len);
    if (dirty) {
        status_buf[pos] = 32;
        pos = pos + 1;
        status_buf[pos] = 42;
        pos = pos + 1;
    }
    status_buf[pos] = 32;
    pos = pos + 1;
    status_buf[pos] = 32;
    pos = pos + 1;

    int row = cursor_row();
    int col = cursor - line_starts[row] + 1;
    pos = status_write_int(row + 1, pos);
    status_buf[pos] = 58;
    pos = pos + 1;
    pos = status_write_int(col, pos);

    draw_bytes(fd, 4, y + (status_h - font_size) / 2, clr_text, font_size, status_buf, pos);
    return 0;
}

/**
 * Draws the word-wrapped buffer line by line (one draw_bytes call per
 * on-screen line -- the renderer's own internal per-glyph advance
 * already matches ch_advance, so a whole line renders correctly in one
 * shot), the selection highlight behind the text, the caret on top, and
 * the linked scrollbar (Phase B) along the right edge. Lines scrolled
 * off-screen (per the scrollbar's current value) are skipped entirely
 * rather than drawn and clipped, since a wrapped file can have far more
 * lines than fit in the fixed-size on-screen row budget this walks.
 */
int draw_buffer(int fd) {
    int sel_active = 0;
    int sel_lo = 0;
    int sel_hi = 0;
    if (anchor >= 0) {
        if (anchor != cursor) {
            sel_active = 1;
            sel_lo = anchor;
            sel_hi = cursor;
            if (sel_lo > sel_hi) {
                int t = sel_lo;
                sel_lo = sel_hi;
                sel_hi = t;
            }
        }
    }

    int scroll_y = sb_get_val();
    int vis = visible_lines();
    int li = 0;
    int caret_x = pane_x;
    int caret_y = pane_y;
    int caret_visible = 0;
    while (li < line_count) {
        int start = line_starts[li];
        int end = file_len;
        if (li + 1 < line_count) {
            end = line_starts[li + 1];
        }
        int screen_row = li - scroll_y;
        int on_screen = 0;
        if (screen_row >= 0) {
            if (screen_row < vis) {
                on_screen = 1;
            }
        }
        int y = pane_y + screen_row * line_h;

        if (on_screen) {
            if (sel_active) {
                if (sel_lo < end) {
                    if (sel_hi > start) {
                        int lo_c = start;
                        if (sel_lo > start) {
                            lo_c = sel_lo;
                        }
                        int hi_c = end;
                        if (sel_hi < end) {
                            hi_c = sel_hi;
                        }
                        if (hi_c > lo_c) {
                            int x1 = line_x_at(start, lo_c);
                            int x2 = line_x_at(start, hi_c);
                            fill_rect(fd, x1, y, x2 - x1, line_h, clr_sel);
                        }
                    }
                }
            }

            int seg_end = end;
            if (seg_end > start) {
                if (file_buf[seg_end - 1] == 10) {
                    seg_end = seg_end - 1;
                }
            }
            int seg_len = seg_end - start;
            if (seg_len > 0) {
                draw_bytes(fd, pane_x, y, clr_text, font_size, file_buf + start, seg_len);
            }
        }

        if (cursor >= start) {
            if (cursor <= end) {
                if (on_screen) {
                    caret_x = line_x_at(start, cursor);
                    caret_y = y;
                    caret_visible = 1;
                }
            }
        }

        li = li + 1;
    }
    if (caret_visible) {
        fill_rect(fd, caret_x, caret_y, 2, caret_h, clr_caret);
    }
    ui_sbar_draw(fd, sb_bar);
    return 0;
}

/**
 * Uppercase-hex digit for a 0-15 nibble.
 */
int nibble_to_hex(int n) {
    if (n < 10) {
        return n + 48;
    }
    return n - 10 + 65;
}

/**
 * Value of a hex-digit key ('0'-'9', 'A'-'F', 'a'-'f'), or -1 if `key`
 * isn't one -- used to gate hex-mode nibble editing so any other keypress
 * (arrows, Tab, Esc, ...) passes through untouched.
 */
int hex_digit_value(int key) {
    if (key >= 48) {
        if (key <= 57) {
            return key - 48;
        }
    }
    if (key >= 65) {
        if (key <= 70) {
            return key - 65 + 10;
        }
    }
    if (key >= 97) {
        if (key <= 102) {
            return key - 97 + 10;
        }
    }
    return -1;
}

/**
 * Renders one hex-dump row into hex_line_buf: a 4-digit address, 16
 * space-separated hex byte pairs, then the same bytes' printable ASCII
 * (or '.' for anything outside 32..126). Same 72-column layout as
 * Quill.lux's hex view (addr at 0-3, hex pairs at 6+i*3, ASCII at 56+i).
 * Always renders all 16 columns -- a column past end-of-file is treated
 * as a phantom zero byte (matching hex_col_x's caret-positioning
 * convention) rather than left blank, so every row's hex-pair and ASCII
 * regions occupy identical width and line up regardless of how many real
 * bytes that row has (a short row rendered with literal blank filler
 * measured narrower than real digit pairs in this proportional font,
 * throwing its ASCII column out of line with the rows above it -- a real
 * bug, caught from a screenshot).
 */
int render_hex_row(int addr) {
    hex_line_buf[0] = nibble_to_hex((addr >> 12) & 15);
    hex_line_buf[1] = nibble_to_hex((addr >> 8) & 15);
    hex_line_buf[2] = nibble_to_hex((addr >> 4) & 15);
    hex_line_buf[3] = nibble_to_hex(addr & 15);
    hex_line_buf[4] = 32;
    hex_line_buf[5] = 32;
    int i = 0;
    while (i < 16) {
        int idx = addr + i;
        int b = 0;
        if (idx < file_len) {
            b = file_buf[idx];
        }
        hex_line_buf[6 + i * 3] = nibble_to_hex((b >> 4) & 15);
        hex_line_buf[6 + i * 3 + 1] = nibble_to_hex(b & 15);
        hex_line_buf[6 + i * 3 + 2] = 32;
        int ch = b;
        if (ch < 32) {
            ch = 46;
        } else {
            if (ch > 126) {
                ch = 46;
            }
        }
        hex_line_buf[56 + i] = ch;
        i = i + 1;
    }
    hex_line_buf[54] = 32;
    hex_line_buf[55] = 32;
    return 0;
}

/**
 * Pixel x of hex column `col` (0-15) within its row -- recomputed from
 * file_buf directly (not hex_line_buf, which only ever holds the most
 * recently rendered row) so the caret lands correctly regardless of draw
 * order. Walks the same fixed 4-digit-address-plus-two-spaces prefix and
 * "XX " triples render_hex_row lays out, using real per-glyph widths
 * (hex digits and spaces don't all measure the same in a proportional
 * font).
 */
int hex_col_x(int row_addr, int col) {
    int x = pane_x;
    x = x + ch_advance(nibble_to_hex((row_addr >> 12) & 15));
    x = x + ch_advance(nibble_to_hex((row_addr >> 8) & 15));
    x = x + ch_advance(nibble_to_hex((row_addr >> 4) & 15));
    x = x + ch_advance(nibble_to_hex(row_addr & 15));
    x = x + ch_advance(32);
    x = x + ch_advance(32);
    int c = 0;
    while (c < col) {
        int idx = row_addr + c;
        int b = 0;
        if (idx < file_len) {
            b = file_buf[idx];
        }
        x = x + ch_advance(nibble_to_hex((b >> 4) & 15));
        x = x + ch_advance(nibble_to_hex(b & 15));
        x = x + ch_advance(32);
        c = c + 1;
    }
    return x;
}

/**
 * Pixel x of the `col`-th ASCII character (0-15) within its row --
 * continues past hex_col_x(row_addr, 16) by the same 2-space gap
 * render_hex_row leaves before the ASCII column (buffer indices 54-55),
 * then walks each preceding ASCII character's own real advance (its
 * printable glyph, or '.' for anything outside 32..126 -- same mapping
 * render_hex_row uses) so it lines up with what's actually drawn there.
 * Used to draw a companion caret in the ASCII column that follows the
 * hex-mode cursor, so it's visible which ASCII character the byte under
 * the hex caret corresponds to.
 */
int hex_ascii_col_x(int row_addr, int col) {
    int x = hex_col_x(row_addr, 16);
    x = x + ch_advance(32) * 2;
    int c = 0;
    while (c < col) {
        int idx = row_addr + c;
        int b = 0;
        if (idx < file_len) {
            b = file_buf[idx];
        }
        int ch = b;
        if (ch < 32) {
            ch = 46;
        } else {
            if (ch > 126) {
                ch = 46;
            }
        }
        x = x + ch_advance(ch);
        c = c + 1;
    }
    return x;
}

/**
 * Draws a 1px hollow rectangle outline -- Fluxio/the VM only expose a
 * filled fill_rect builtin, so a hollow box is four thin fill_rect calls
 * along its edges rather than one call.
 */
int draw_rect_outline(int fd, int x, int y, int w, int h, int color) {
    fill_rect(fd, x, y, w, 1, color);
    fill_rect(fd, x, y + h - 1, w, 1, color);
    fill_rect(fd, x, y, 1, h, color);
    fill_rect(fd, x + w - 1, y, 1, h, color);
    return 0;
}

/**
 * Draws the buffer as a hex dump, 16 bytes per row, and the caret at the
 * cursor's row/column -- scrolled per the shared `sb_bar` (re-ranged to
 * row units by update_scrollbar_geometry/toggle_hex_mode). Rows scrolled
 * off-screen are skipped entirely, same reasoning as draw_buffer. View-only
 * for now -- see the file header.
 */
int draw_hex_buffer(int fd) {
    int scroll_y = sb_get_val();
    int vis = visible_lines();
    int addr = 0;
    int row = 0;
    while (addr <= file_len) {
        int screen_row = row - scroll_y;
        if (screen_row >= 0) {
            if (screen_row < vis) {
                render_hex_row(addr);
                int y = pane_y + screen_row * line_h;
                draw_bytes(fd, pane_x, y, clr_text, font_size, hex_line_buf, 72);
            }
        }
        if (addr == file_len) {
            addr = addr + 17;
        } else {
            addr = addr + 16;
        }
        row = row + 1;
    }

    int caret_row = cursor / 16;
    int caret_screen_row = caret_row - scroll_y;
    if (caret_screen_row >= 0) {
        if (caret_screen_row < vis) {
            int caret_col = cursor % 16;
            int caret_x = hex_col_x(caret_row * 16, caret_col);
            int b = 0;
            if (cursor < file_len) {
                b = file_buf[cursor];
            }
            int high_ch = nibble_to_hex((b >> 4) & 15);
            int nibble_ch = high_ch;
            if (hex_nibble) {
                caret_x = caret_x + ch_advance(high_ch);
                nibble_ch = nibble_to_hex(b & 15);
            }
            int caret_y = pane_y + caret_screen_row * line_h;
            /* Hollow blue rectangle, not the solid red bar text mode
             * uses -- distinguishes "selecting a nibble" from "inserting
             * between characters", and its width hugs the actual digit
             * glyph rather than a fixed 2px. */
            draw_rect_outline(fd, caret_x, caret_y, ch_advance(nibble_ch), caret_h, clr_hex_caret);

            /* Companion box in the ASCII column, same row, following the
             * same byte -- so it's visible which ASCII character the
             * byte under the hex caret corresponds to. */
            int ascii_ch = b;
            if (ascii_ch < 32) {
                ascii_ch = 46;
            } else {
                if (ascii_ch > 126) {
                    ascii_ch = 46;
                }
            }
            int ascii_x = hex_ascii_col_x(caret_row * 16, caret_col);
            draw_rect_outline(fd, ascii_x, caret_y, ch_advance(ascii_ch), caret_h, clr_hex_caret);
        }
    }
    return 0;
}

/**
 * Maps a click point in hex mode to the nearest byte index, using the
 * shared scroll offset (row units) and hex_col_x for the reverse x->column
 * lookup (walking columns until one's right edge passes the click, same
 * "nearest cell" rule find_click_index uses for text mode).
 */
int hex_find_click_index(int mx, int my) {
    int scroll_y = sb_get_val();
    int vis = visible_lines();
    int row_in_view = (my - pane_y) / line_h;
    if (row_in_view < 0) {
        row_in_view = 0;
    }
    if (row_in_view >= vis) {
        row_in_view = vis - 1;
    }
    int row = scroll_y + row_in_view;
    int row_addr = row * 16;
    if (row_addr > file_len) {
        row_addr = (file_len / 16) * 16;
    }
    int col = 0;
    int result_col = 15;
    int done = 0;
    while (col < 16) {
        if (done == 0) {
            int x1 = hex_col_x(row_addr, col + 1);
            if (mx < x1) {
                result_col = col;
                done = 1;
            }
        }
        col = col + 1;
    }
    int idx = row_addr + result_col;
    if (idx > file_len) {
        idx = file_len;
    }
    return idx;
}

/**
 * Entry point.
 */
int main() {
    int fd = vfs_open("/dev/draw");
    if (fd < 0) {
        return -1;
    }
    set_chan(fd, 1); /* k8 grayscale */
    int kfd = vfs_open("/dev/kbd");
    int mfd = vfs_open("/dev/mouse");
    set_window_title("Quill (Fluxio)");
    set_font(fd, 0);
    load_font_widths();

    int size = canvas_size(fd);
    canvas_w = size >> 16;
    canvas_h = size & 0xFFFF;
    /* SF::show centers its dialog on APP::width/height, but Quill.fx
     * bypasses APP::init/loop entirely (its own self-contained loop), so
     * those globals would otherwise stay 0 -- see APP::win-set!'s own
     * doc comment (lib/app.lux). */
    app_win_set(canvas_w, canvas_h);
    escmenu_init(canvas_w, canvas_h);

    /* UI::draw's own menu-bar rendering (mb-draw-bar/mb-draw-drop) draws
     * through DRAW::fd (lib/draw.lux), a Lux-side global set only by
     * DRAW::use -- UI::sbar-draw and our own draw_bytes/fill_rect calls
     * all take an fd argument directly and don't need this, which is why
     * the scrollbar and status bar render fine without it, but the menu
     * bar silently drew to fd 0 (never explicitly set to anything) until
     * this call was added. */
    draw_use(fd);
    ui_new();
    ui_menubar(canvas_w);
    ui_menu(menu_file_label);
    ui_item(menu_new_label, 0);
    ui_item(menu_open_label, 0);
    ui_item(menu_save_label, 0);
    ui_item(menu_saveas_label, 0);
    ui_item(menu_quit_label, 0);
    ui_menu(menu_edit_label);
    ui_item(menu_cut_label, 0);
    ui_item(menu_copy_label, 0);
    ui_item(menu_paste_label, 0);
    ui_item(menu_selectall_label, 0);
    ui_menu(menu_view_label);
    ui_check_item(menu_hex_label, 0);
    ui_menu(menu_font_label);
    ui_radio_item(menu_chicago_label, font_grp, 0);
    ui_radio_item(menu_geneva_label, font_grp, 0);
    ui_radio_item(menu_monaco_label, font_grp, 0);
    apply_text_font(fd, 0);

    load_file();

    while (1) {
        /* The menu bar (feed/poll/draw) is always live, in both text and
         * hex mode -- only the scrollbar and text-pane click handling
         * below are text-mode-specific (hex mode has neither yet). This
         * was a real bug, not a design choice: gating the whole mouse
         * block (and the draw call further down) on `!hex_mode` meant
         * switching to hex mode made the menu bar vanish and stop
         * responding entirely, since nothing fed it events or drew it. */
        /* Drains every queued mouse event this frame, not just one --
         * poll_mouse() pops a single event per call (src/vfs.c), but the
         * host can enqueue several MOUSEMOTION events per ~13ms frame
         * (a fast mouse/trackpad easily outpaces a 60-75Hz app loop). An
         * `if` here (the original shape) only drained one event/frame,
         * so the 64-slot queue (SYS_INPUT_QUEUE_SZ, include/system.h)
         * backed up during any real mouse movement -- hover highlighting
         * and click response would visibly lag behind the real cursor by
         * however many frames it took to catch up, then "catch up all at
         * once" the moment the mouse stopped moving. Reported by the user
         * as menu-open/click/close all feeling delayed by roughly a
         * second. A `while` drains the backlog to zero every frame
         * instead of growing it. */
        if (escmenu_wants_quit()) {
            vfs_close(fd);
            vfs_close(kfd);
            vfs_close(mfd);
            return 0;
        }
        if (escmenu_wants_restart()) {
            escmenu_ack_restart();
            restart_app(fd);
        }

        int had_mouse = poll_mouse(mfd);
        while (had_mouse) {
            int mtype = mouse_type();
            int mx = mouse_x();
            int my = mouse_y();

            /* Route every mouse event through the linked menu bar first
             * (UI::feed) -- it needs MOUSE_MOVE too, to track hover
             * highlighting while a dropdown is open, not just clicks.
             * `was_open || now_open` catches both edges: the down-click
             * that *opens* a menu (open only after feed) and the click
             * on an item that *closes* one (open only before feed) --
             * either way the event was menu business, not the text
             * pane's. */
            ui_mpkt[0] = mtype;
            ui_mpkt[1] = mouse_button();
            ui_mpkt[2] = mx & 0xFF;
            ui_mpkt[3] = (mx >> 8) & 0xFF;
            ui_mpkt[4] = my & 0xFF;
            ui_mpkt[5] = (my >> 8) & 0xFF;
            ui_mpkt[6] = 0;
            ui_mpkt[7] = 0;

            /* File > New's confirm dialog is the highest-precedence modal
             * of all -- while it's open, not even the file picker or the
             * menu bar's own UI::feed sees this event, same reasoning as
             * the SF picker's own precedence over the menu bar below. */
            if (confirm_new_open) {
                confirm_new_mouse(mtype, mouse_button(), mx, my);
            } else {
            /* The file picker is modal (Quill.lux's SF::show calls
             * APP::modal!): while it's open, every mouse event goes to it
             * alone, not even through the menu bar's own UI::feed, same
             * as a real modal dialog blocking input to what's under it. */
            if (sf_is_open()) {
                sf_mouse(ui_mpkt);
            } else {
            /* The Esc sheet sits below both modals and above the menu bar
             * and text pane, mirroring the keyboard chain's order exactly
             * -- while the sheet is up it owns the mouse, so a click on
             * Continue / Restart App / Quit can't also land on whatever is
             * underneath it. */
            if (escmenu_mouse(mtype, mouse_button(), mx, my)) {
            } else {
                int menu_was_open = ui_menu_open();
                ui_feed(ui_mpkt);
                int menu_now_open = ui_menu_open();

                if (menu_was_open) {
                } else {
                    if (menu_now_open) {
                    } else {
                        if (mtype == 3) {
                            if (ui_in_rect(mx, my, sb_x, sb_y, sb_w, sb_h)) {
                                ui_sbar_press(sb_bar, mx, my);
                                sb_dragging = 1;
                            } else {
                                if (hex_mode) {
                                    cursor = hex_find_click_index(mx, my);
                                    hex_nibble = 0;
                                } else {
                                    int idx = find_click_index(mx, my);
                                    cursor = idx;
                                    anchor = idx;
                                    mouse_held = 1;
                                }
                            }
                        } else {
                            if (mtype == 2) {
                                if (sb_dragging) {
                                    ui_sbar_drag(sb_bar, mx, my);
                                } else {
                                    if (!hex_mode) {
                                        if (mouse_held) {
                                            cursor = find_click_index(mx, my);
                                        }
                                    }
                                }
                            } else {
                                if (mtype == 4) {
                                    if (sb_dragging) {
                                        ui_sbar_release(sb_bar);
                                        sb_dragging = 0;
                                    }
                                    mouse_held = 0;
                                }
                            }
                        }
                    }
                }
            }
            }
            }
            had_mouse = poll_mouse(mfd);
        }

        if (ui_poll_next()) {
            int fired = ui_poll_name();
            if (fired == menu_new_label) {
                if (dirty) {
                    confirm_new_open = 1;
                } else {
                    new_document();
                }
            } else {
            if (fired == menu_open_label) {
                sf_show();
            } else {
                if (fired == menu_save_label) {
                    save_file();
                } else {
                    if (fired == menu_saveas_label) {
                        save_as = 1;
                        sf_show_save(status_label);
                    } else {
                    if (fired == menu_quit_label) {
                        vfs_close(fd);
                        vfs_close(kfd);
                        vfs_close(mfd);
                        return 0;
                    } else {
                        if (fired == menu_hex_label) {
                            toggle_hex_mode();
                        } else {
                            if (fired == menu_cut_label) {
                                if (!hex_mode) {
                                    copy_selection();
                                    delete_selection();
                                }
                            } else {
                                if (fired == menu_copy_label) {
                                    if (!hex_mode) {
                                        copy_selection();
                                    }
                                } else {
                                    if (fired == menu_paste_label) {
                                        paste_snarf();
                                    } else {
                                        if (fired == menu_selectall_label) {
                                            if (!hex_mode) {
                                                select_all();
                                            }
                                        } else {
                                            if (fired == menu_chicago_label) {
                                                apply_text_font(fd, 0);
                                            } else {
                                                if (fired == menu_geneva_label) {
                                                    apply_text_font(fd, 2);
                                                } else {
                                                    if (fired == menu_monaco_label) {
                                                        apply_text_font(fd, 3);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    }
                }
            }
            }
        }

        if (sf_picked()) {
            if (save_as) {
                save_as = 0;
                save_picked_file();
            } else {
                open_picked_file();
            }
            sf_clear_result();
        } else {
            if (sf_cancelled()) {
                save_as = 0;
                sf_clear_result();
            }
        }

        /* Same backlog fix as the mouse loop above -- drains every
         * queued keydown this frame instead of just one, so fast typing
         * can't fall behind. */
        int had_kbd = poll_kbd(kfd);
        while (had_kbd) {
            if (confirm_new_open) {
                /* Same highest-precedence-modal reasoning as the mouse
                 * path above. */
                if (kbd_type() == 0) {
                    confirm_new_kbd(kbd_key());
                }
            } else {
            if (sf_is_open()) {
                /* Same modal precedence as the mouse path above -- while
                 * the picker is open, it owns the keyboard too (Esc
                 * cancels the dialog there, it doesn't fall through to
                 * Quill's own quit handling below). */
                sf_kpkt[0] = kbd_type();
                sf_kpkt[1] = 0;
                int sf_key = kbd_key();
                sf_kpkt[2] = sf_key & 0xFF;
                sf_kpkt[3] = (sf_key >> 8) & 0xFF;
                sf_kpkt[4] = 0;
                sf_kpkt[5] = 0;
                sf_kpkt[6] = 0;
                sf_kpkt[7] = 0;
                sf_kbd(sf_kpkt);
            } else {
            /* The Esc sheet sits below both modals and above Quill's own
             * keys: while the confirm panel or the file picker is up, Esc
             * belongs to that dialog (it cancels it) and must not raise
             * the system menu behind it -- docs/user-manual.md's "System
             * menu (Esc)" spells that precedence out, and it's the same
             * order APP::modal! gives Lux apps. escmenu_kbd, not
             * escmenu_key: it drops the KEY_UP half of each press, and
             * since Esc *toggles*, feeding it both halves would open the
             * sheet on the down and shut it again on the up. */
            if (escmenu_kbd(kbd_type(), kbd_key())) {
            } else {
            if (kbd_type() == 0) {
                int key = kbd_key();
                if (key == 23) {
                    toggle_hex_mode();
                } else {
                    if (key == 9) {
                        save_file();
                    } else {
                        if (0) {
                            /* Esc used to quit here, discarding unsaved
                             * edits without a prompt. It never reaches
                             * this branch now -- escmenu_kbd above
                             * consumes 27 to open the System Menu, whose
                             * Quit button is the deliberate way out. */
                        } else {
                            if (hex_mode) {
                                if (key == 19) {
                                    if (cursor > 0) {
                                        cursor = cursor - 1;
                                    }
                                    hex_nibble = 0;
                                } else {
                                    if (key == 20) {
                                        if (cursor < file_len) {
                                            cursor = cursor + 1;
                                        }
                                        hex_nibble = 0;
                                    } else {
                                        if (key == 17) {
                                            if (cursor - 16 >= 0) {
                                                cursor = cursor - 16;
                                            }
                                            hex_nibble = 0;
                                        } else {
                                            if (key == 18) {
                                                if (cursor + 16 <= file_len) {
                                                    cursor = cursor + 16;
                                                }
                                                hex_nibble = 0;
                                            } else {
                                                int digit = hex_digit_value(key);
                                                if (digit >= 0) {
                                                    if (cursor < file_len) {
                                                        edit_hex_nibble(digit);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                int has_sel = 0;
                                if (anchor >= 0) {
                                    if (anchor != cursor) {
                                        has_sel = 1;
                                    }
                                }
                                if (key == 8) {
                                    if (has_sel) {
                                        delete_selection();
                                    } else {
                                        delete_char();
                                    }
                                } else {
                                    if (key == 13) {
                                        if (has_sel) {
                                            delete_selection();
                                        }
                                        insert_char(10);
                                    } else {
                                        if (key == 19) {
                                            if (has_sel) {
                                                int lo = anchor;
                                                if (cursor < lo) {
                                                    lo = cursor;
                                                }
                                                cursor = lo;
                                                anchor = -1;
                                            } else {
                                                if (cursor > 0) {
                                                    cursor = cursor - 1;
                                                }
                                            }
                                        } else {
                                            if (key == 20) {
                                                if (has_sel) {
                                                    int hi = anchor;
                                                    if (cursor > hi) {
                                                        hi = cursor;
                                                    }
                                                    cursor = hi;
                                                    anchor = -1;
                                                } else {
                                                    if (cursor < file_len) {
                                                        cursor = cursor + 1;
                                                    }
                                                }
                                            } else {
                                                if (key >= 32) {
                                                    if (key <= 126) {
                                                        if (has_sel) {
                                                            delete_selection();
                                                        }
                                                        insert_char(key);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            }
            }
            }
            had_kbd = poll_kbd(kfd);
        }

        if (cursor != last_cursor) {
            ensure_cursor_visible();
            last_cursor = cursor;
        }

        begin_frame(fd);
        fill_rect(fd, 0, 0, canvas_w, canvas_h, clr_bg);
        set_font(fd, app_font);
        if (hex_mode) {
            draw_hex_buffer(fd);
        } else {
            draw_buffer(fd);
        }
        set_font(fd, 0);
        draw_status_line(fd);
        ui_draw();
        if (sf_is_open()) {
            sf_draw();
        }
        confirm_new_draw(fd);
        /* The sheet is drawn last so it sits over everything, and with
         * the Chicago face explicitly selected -- ui_draw()/sf_draw()
         * above leave whatever face they last used current, and
         * escape_menu.fx deliberately calls nothing but fill_rect/
         * draw_str, so it can't pin the font itself. lib/app.lux's
         * draw-esc-popup does the same set-font first. */
        set_font(fd, 0);
        escmenu_draw(fd);
        end_frame(fd);
        yield();
    }

    vfs_close(fd);
    return 0;
}
