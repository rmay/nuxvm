/* Quill (Fluxio port, v9 -- word-wrap + mouse selection + hex view +
 * scrollbar (text and hex mode both) + status bar + viewport auto-follow +
 * menu bar) -- docs/quill_fluxio.md Phase C.
 *
 * Port of apps/Quill.lux's core edit loop: open a file, display it with
 * word-wrap, move the cursor (keyboard or mouse click), select text by
 * dragging, insert/delete characters, save, view it as hex, scroll with
 * a real scrollbar (which follows the cursor on its own past the edge of
 * the pane), see filename/dirty/row:col in a status bar, and use a real
 * File/View menu bar (feeds/draws/fires in both text and hex mode) --
 * all via extern bindings into the Phase B linked UI library
 * (lib/uisf.bin) instead of being pure self-contained Fluxio. Still NOT
 * at feature parity -- the file picker (same linked library) comes
 * next. Known simplifications vs Quill.lux, to be closed in a later
 * pass:
 *   - Fixed file path (no file picker yet) -- /sys/file/quill_scratch.txt.
 *   - No shift-to-extend-selection from the keyboard -- Fluxio has no
 *     keyboard-modifier accessor builtin today, so selection is
 *     mouse-drag only (click sets the anchor, drag extends, release
 *     keeps it). Arrow keys collapse an active selection instead.
 *   - Hex mode still has no nibble editing (unlike Quill.lux's hex-mode
 *     editing) -- the cursor can only move via arrow keys or a mouse
 *     click, not edit bytes in place. It does now scroll (the shared
 *     sb_bar scrollbar is re-ranged in 16-byte-row units) and support
 *     click-to-position (hex_find_click_index), same as text mode.
 *   - The menu only has File > Save/Quit and View > Toggle Hex --
 *     there's no Edit menu (cut/copy/paste aren't implemented at all
 *     yet) and no keyboard shortcuts shown or bound for any item.
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

/* Trampoline slot addresses are abi/uisf.exports.json's committed,
 * append-only order: MM_ABI_LIBRARY_LINK_BASE (0x700000) + 12 +
 * 5*index. Built and linked by the apps/fluxio/Quill.bin Makefile rule
 * (luxc -base 0x701000 for the library, then fluxlink --lib-base
 * 0x700000 to merge it with this file's own compiled output). See
 * docs/quill_fluxio.md Phase B7. */
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
extern void ui_feed(int mpkt) = 0x700048;                                           /* index 12 */
extern void ui_draw() = 0x70004D;                                                   /* index 13 */
extern int ui_menu_open() = 0x700057;                                               /* index 15 */
extern int ui_poll_next() = 0x70005C;                                               /* index 16 */
extern int ui_poll_name() = 0x700061;                                               /* index 17 */
extern void draw_use(int fd) = 0x7000A7;                                            /* index 31 */

byte file_buf[65536];
byte font_widths[256];
byte hex_line_buf[72];
byte scratch_path[32] = "/sys/file/quill_scratch.txt";
byte sb_bar[56];
byte status_label[20] = "quill_scratch.txt";
byte status_buf[64];
byte ui_mpkt[8];
byte menu_file_label[8] = "File";
byte menu_save_label[8] = "Save";
byte menu_quit_label[8] = "Quit";
byte menu_view_label[8] = "View";
byte menu_hex_label[16] = "Toggle Hex";
int file_len;
int cursor;
int anchor = -1;
int mouse_held;
int hex_mode;
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
int clr_sel = 0xCCDDFF;
int clr_frame = 0xAAAAAA;
int clr_status_bg = 0xEEEEEE;

/**
 * Pixel advance of one glyph at font_size, from the proportional font
 * width table Quill.lux also uses (/sys/font/widths, 16px-nominal
 * widths scaled to font_size -- exactly matches the renderer's own
 * internal system_measure_char formula, so this stays pixel-accurate
 * with what draw_bytes/draw_str actually draw). Newline has no width. A
 * zero table entry (glyph not covered by the table) falls back to 6px,
 * same as Quill.lux.
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
 */
int toggle_hex_mode() {
    if (hex_mode) {
        hex_mode = 0;
    } else {
        hex_mode = 1;
    }
    anchor = -1;
    mouse_held = 0;
    update_scrollbar_geometry();
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
 * Loads /sys/font/widths into font_widths (silently leaves it zeroed --
 * every advance falls back to the 6px default -- if the pseudo-file is
 * ever missing, rather than failing the whole program over cosmetics).
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
 * Loads the scratch file into file_buf. Tries read+write first (an
 * existing file); if that fails (file doesn't exist yet), creates it
 * empty instead -- vfs_open_buf/create_host_file has no single flag
 * combination for "open existing or create, without truncating either
 * way" today, so this is the two-step workaround.
 */
int load_file() {
    int fd = vfs_open_buf(scratch_path, 28, 2);
    if (fd < 0) {
        fd = vfs_open_buf(scratch_path, 28, 6);
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
    int fd = vfs_open_buf(scratch_path, 28, 6);
    if (fd < 0) {
        return 0;
    }
    vfs_write(fd, file_buf, file_len);
    vfs_close(fd);
    dirty = 0;
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

    int pos = status_copy(0, status_label, 17);
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
            int caret_y = pane_y + caret_screen_row * line_h;
            fill_rect(fd, caret_x, caret_y, 2, caret_h, clr_caret);
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
    int kfd = vfs_open("/dev/kbd");
    int mfd = vfs_open("/dev/mouse");
    set_window_title("Quill (Fluxio)");
    load_font_widths();

    int size = canvas_size(fd);
    canvas_w = size >> 16;
    canvas_h = size & 0xFFFF;

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
    ui_item(menu_save_label, 0);
    ui_item(menu_quit_label, 0);
    ui_menu(menu_view_label);
    ui_item(menu_hex_label, 0);

    load_file();

    while (1) {
        /* The menu bar (feed/poll/draw) is always live, in both text and
         * hex mode -- only the scrollbar and text-pane click handling
         * below are text-mode-specific (hex mode has neither yet). This
         * was a real bug, not a design choice: gating the whole mouse
         * block (and the draw call further down) on `!hex_mode` meant
         * switching to hex mode made the menu bar vanish and stop
         * responding entirely, since nothing fed it events or drew it. */
        int had_mouse = poll_mouse(mfd);
        if (had_mouse) {
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

        if (ui_poll_next()) {
            int fired = ui_poll_name();
            if (fired == menu_save_label) {
                save_file();
            } else {
                if (fired == menu_quit_label) {
                    vfs_close(fd);
                    vfs_close(kfd);
                    vfs_close(mfd);
                    return 0;
                } else {
                    if (fired == menu_hex_label) {
                        toggle_hex_mode();
                    }
                }
            }
        }

        int had_kbd = poll_kbd(kfd);
        if (had_kbd) {
            if (kbd_type() == 0) {
                int key = kbd_key();
                if (key == 23) {
                    toggle_hex_mode();
                } else {
                    if (key == 9) {
                        save_file();
                    } else {
                        if (key == 27) {
                            vfs_close(fd);
                            vfs_close(kfd);
                            vfs_close(mfd);
                            return 0;
                        } else {
                            if (hex_mode) {
                                if (key == 19) {
                                    if (cursor > 0) {
                                        cursor = cursor - 1;
                                    }
                                } else {
                                    if (key == 20) {
                                        if (cursor < file_len) {
                                            cursor = cursor + 1;
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

        if (cursor != last_cursor) {
            ensure_cursor_visible();
            last_cursor = cursor;
        }

        begin_frame(fd);
        fill_rect(fd, 0, 0, canvas_w, canvas_h, clr_bg);
        if (hex_mode) {
            draw_hex_buffer(fd);
        } else {
            draw_buffer(fd);
        }
        draw_status_line(fd);
        ui_draw();
        end_frame(fd);
        yield();
    }

    vfs_close(fd);
    return 0;
}
