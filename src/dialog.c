#include "dialog.h"
#include "chicago.h"
#include "cff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include <SDL.h>

// Dialog geometry: 400x300, list 330x210, item height 20, scrollbar 20.
#define DLG_W 400
#define DLG_H 300
#define DLG_LIST_W 330
#define DLG_LIST_H 210
#define DLG_ITEM_H 20
#define DLG_SB_W 20
#define DLG_BTN_W 70
#define DLG_BTN_H 30
#define DLG_DOUBLE_CLICK_MS 500

static const uint16_t icon_floppy[] = {
    0b1111111111110000,
    0b1000000000011000,
    0b1011111111010100,
    0b1010000001010010,
    0b1010000001010001,
    0b1011111111010001,
    0b1000000000010001,
    0b1000000000010001,
    0b1001111110010001,
    0b1001000010010001,
    0b1001000010010001,
    0b1001111110010001,
    0b1000000000010001,
    0b1111111111110001,
    0b0000000000001111,
};
static const uint16_t icon_folder[] = {
    0b0111000000000000,
    0b1000111111111100,
    0b1000000000000110,
    0b1111111111111111,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1000000000000001,
    0b1111111111111111,
};
static const uint16_t icon_doc[] = {
    0b1111111111000000,
    0b1000000001100000,
    0b1000000001010000,
    0b1000000001111000,
    0b1000000000000100,
    0b1000000000000100,
    0b1000000000000100,
    0b1000000000000100,
    0b1000000000000100,
    0b1000000000000100,
    0b1111111111111100,
};
static const uint16_t icon_up_arrow[] = {
    0b000001000000,
    0b000011100000,
    0b000111110000,
    0b001111111000,
    0b011111111100,
};
static const uint16_t icon_down_arrow[] = {
    0b011111111100,
    0b001111111000,
    0b000111110000,
    0b000011100000,
    0b000001000000,
};

// --- framebuffer helpers: draw into sys->screen_pixels ([A,R,G,B] bytes) ---

static void dlg_fill_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    uint8_t* fb = sys->screen_pixels;
    if (!fb) return;
    int32_t sw = sys->screen_width, sh = sys->screen_height;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    for (int32_t py = y; py < y + h; py++) {
        if (py < 0 || py >= sh) continue;
        for (int32_t px = x; px < x + w; px++) {
            if (px < 0 || px >= sw) continue;
            uint8_t* p = fb + ((size_t)py * sw + px) * 4;
            p[0] = 0xFF; p[1] = r; p[2] = g; p[3] = b;
        }
    }
}

static void dlg_draw_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    dlg_fill_rect(sys, x, y, w, 1, color);
    dlg_fill_rect(sys, x, y + h - 1, w, 1, color);
    dlg_fill_rect(sys, x, y, 1, h, color);
    dlg_fill_rect(sys, x + w - 1, y, 1, h, color);
}

static void dlg_draw_bitmap(System* sys, int32_t x, int32_t y, const uint16_t* bitmap, int rows, uint32_t color) {
    uint8_t* fb = sys->screen_pixels;
    if (!fb) return;
    int32_t sw = sys->screen_width, sh = sys->screen_height;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    for (int row = 0; row < rows; row++) {
        uint16_t bits = bitmap[row];
        for (int col = 0; col < 16; col++) {
            if (((bits >> (15 - col)) & 1) == 0) continue;
            int32_t px = x + col, py = y + row;
            if (px < 0 || px >= sw || py < 0 || py >= sh) continue;
            uint8_t* p = fb + ((size_t)py * sw + px) * 4;
            p[0] = 0xFF; p[1] = r; p[2] = g; p[3] = b;
        }
    }
}

// Draw one Chicago glyph at scale 1 into the front buffer. Returns the
// x-advance (glyph width + 1px spacing).
static int dlg_draw_char(System* sys, int32_t x, int32_t y, char c, uint32_t color) {
    uint8_t* fb = sys->screen_pixels;
    if (!fb) return 0;
    int32_t sw = sys->screen_width, sh = sys->screen_height;
    unsigned char* data = chicago12x12_cff;
    int width = data[(uint8_t)c];
    if (width == 0) {
        if (c == ' ') return 7;
        return 0;
    }

    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    int tile_size = cff_tile_size((int)chicago12x12_cff_len);
    if (tile_size <= 0) tile_size = 16;
    int num_h = tile_size / 8;
    int num_v = tile_size / 8;
    int tile_count = num_h * num_v;
    int offset = 256 + (uint8_t)c * tile_count * 8;

    int idx = 0;
    for (int tx = 0; tx < num_h; tx++) {
        for (int ty = 0; ty < num_v; ty++) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = data[offset + idx++];
                if (bits == 0) continue;
                int32_t py = y + ty * 8 + row;
                if (py < 0 || py >= sh) continue;
                for (int col = 0; col < 8; col++) {
                    if ((bits & (0x80 >> col)) == 0) continue;
                    int32_t px = x + tx * 8 + col;
                    if (px < 0 || px >= sw) continue;
                    uint8_t* p = fb + ((size_t)py * sw + px) * 4;
                    p[0] = 0xFF; p[1] = r; p[2] = g; p[3] = b;
                }
            }
        }
    }
    return width + 1;
}

static void dlg_draw_str(System* sys, int32_t x, int32_t y, const char* s, uint32_t color) {
    int32_t cur = x;
    for (; *s; s++) {
        cur += dlg_draw_char(sys, cur, y, *s, color);
    }
}

// --- path helpers (Go: path.Dir / path.Join on the relative dialog path) ---

static void path_dirname(char* path) {
    char* slash = strrchr(path, '/');
    if (!slash) {
        strcpy(path, ".");
    } else {
        *slash = '\0';
        if (path[0] == '\0') strcpy(path, "/");
    }
}

static void path_join(char* dst, size_t dstlen, const char* base, const char* name) {
    if (strcmp(base, ".") == 0) {
        snprintf(dst, dstlen, "%s", name);
    } else {
        snprintf(dst, dstlen, "%s/%s", base, name);
    }
}

// --- listing ---

static int entry_priority(const DialogEntry* e) {
    if (strcmp(e->name, "..") == 0) return 0;
    bool hidden = e->name[0] == '.';
    if (e->is_dir) return hidden ? 1 : 2;
    return hidden ? 3 : 4;
}

static int entry_cmp(const void* a, const void* b) {
    const DialogEntry* ea = (const DialogEntry*)a;
    const DialogEntry* eb = (const DialogEntry*)b;
    int pa = entry_priority(ea), pb = entry_priority(eb);
    if (pa != pb) return pa - pb;
    int ci = strcasecmp(ea->name, eb->name);
    if (ci != 0) return ci;
    return strcmp(ea->name, eb->name);
}

static void add_entry(FileDialog* d, const char* name, bool is_dir) {
    DialogEntry* grown = realloc(d->entries, (d->count + 1) * sizeof(DialogEntry));
    if (!grown) return;
    d->entries = grown;
    strncpy(d->entries[d->count].name, name, sizeof(d->entries[d->count].name) - 1);
    d->entries[d->count].name[sizeof(d->entries[d->count].name) - 1] = '\0';
    d->entries[d->count].is_dir = is_dir;
    d->count++;
}

static void refresh_files(FileDialog* d) {
    free(d->entries);
    d->entries = NULL;
    d->count = 0;

    const char* root = (d->sys && d->sys->sandbox_root[0]) ? d->sys->sandbox_root : ".";
    char host[1024];
    if (strcmp(d->path, ".") == 0) {
        snprintf(host, sizeof(host), "%s", root);
    } else {
        snprintf(host, sizeof(host), "%s/%s", root, d->path);
    }

    if (strcmp(d->path, ".") != 0 && strcmp(d->path, "/") != 0) {
        add_entry(d, "..", true);
    }

    DIR* dir = opendir(host);
    if (!dir) {
        add_entry(d, "error: cannot open directory", false);
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        bool is_dir = false;
        if (de->d_type == DT_DIR) {
            is_dir = true;
        } else if (de->d_type == DT_UNKNOWN) {
            char full[1400];
            snprintf(full, sizeof(full), "%s/%s", host, de->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = true;
        }
        add_entry(d, de->d_name, is_dir);
    }
    closedir(dir);

    qsort(d->entries, d->count, sizeof(DialogEntry), entry_cmp);
}

// --- lifecycle / input ---

void dialog_open(FileDialog* d, System* sys) {
    dialog_free(d);
    d->sys = sys;
    strcpy(d->path, ".");
    d->selected = 0;
    d->scroll = 0;
    d->last_click_idx = -1;
    d->last_click_ms = 0;
    refresh_files(d);
    d->active = true;
}

void dialog_free(FileDialog* d) {
    free(d->entries);
    d->entries = NULL;
    d->count = 0;
    d->active = false;
}

// Open the selected entry: descend into directories (stays active), or
// deliver a file path result and close.
static void open_selected(FileDialog* d) {
    if (d->selected < 0 || d->selected >= d->count) return;

    DialogEntry* e = &d->entries[d->selected];
    if (e->is_dir) {
        if (strcmp(e->name, "..") == 0) {
            path_dirname(d->path);
        } else {
            char next[512];
            path_join(next, sizeof(next), d->path, e->name);
            strncpy(d->path, next, sizeof(d->path) - 1);
            d->path[sizeof(d->path) - 1] = '\0';
        }
        d->selected = 0;
        d->scroll = 0;
        refresh_files(d);
        return;
    }

    char full[768];
    path_join(full, sizeof(full), d->path, e->name);
    system_set_dialog_result(d->sys, full);
    d->active = false;
}

static void dialog_cancel(FileDialog* d) {
    system_set_dialog_result(d->sys, "cancel");
    d->active = false;
}

void dialog_key(FileDialog* d, int32_t code) {
    int visible = DLG_LIST_H / DLG_ITEM_H;
    if (code == 17) { // Arrow Up
        d->selected--;
        if (d->selected < 0) d->selected = 0;
        if (d->selected < d->scroll) d->scroll = d->selected;
    } else if (code == 18) { // Arrow Down
        d->selected++;
        if (d->selected >= d->count) d->selected = d->count - 1;
        if (d->selected >= d->scroll + visible) d->scroll = d->selected - visible + 1;
    } else if (code == 13) { // Enter
        open_selected(d);
    } else if (code == 27) { // Esc
        dialog_cancel(d);
    }
}

void dialog_wheel(FileDialog* d, int32_t dy) {
    int visible = DLG_LIST_H / DLG_ITEM_H;
    d->scroll -= dy;
    if (d->scroll < 0) d->scroll = 0;
    int max_scroll = d->count - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (d->scroll > max_scroll) d->scroll = max_scroll;
}

void dialog_mouse_down(FileDialog* d, int32_t mx, int32_t my) {
    int32_t w = d->sys->screen_width, h = d->sys->screen_height;
    int32_t dx = (w - DLG_W) / 2, dy = (h - DLG_H) / 2;
    int32_t lb_x = dx + 20, lb_y = dy + 40;
    int visible = DLG_LIST_H / DLG_ITEM_H;

    // 1. Buttons
    int32_t bx = dx + DLG_W - 100, by = dy + DLG_H - 45;
    if (mx >= bx && mx <= bx + DLG_BTN_W && my >= by && my <= by + DLG_BTN_H) {
        open_selected(d);
        return;
    }
    bx -= 90;
    if (mx >= bx && mx <= bx + DLG_BTN_W && my >= by && my <= by + DLG_BTN_H) {
        dialog_cancel(d);
        return;
    }

    // 2. Scrollbar arrows
    if (mx >= lb_x + DLG_LIST_W - DLG_SB_W && mx <= lb_x + DLG_LIST_W) {
        if (my >= lb_y && my <= lb_y + DLG_SB_W) {
            d->scroll--;
            if (d->scroll < 0) d->scroll = 0;
        } else if (my >= lb_y + DLG_LIST_H - DLG_SB_W && my <= lb_y + DLG_LIST_H) {
            d->scroll++;
            if (d->scroll > d->count - visible) {
                d->scroll = d->count - visible;
                if (d->scroll < 0) d->scroll = 0;
            }
        }
    }

    // 3. File list rows (double-click opens)
    if (mx >= lb_x && mx <= lb_x + DLG_LIST_W - DLG_SB_W &&
        my >= lb_y && my <= lb_y + DLG_LIST_H) {
        int idx = (my - lb_y) / DLG_ITEM_H;
        if (idx >= 0 && idx < visible) {
            int actual = idx + d->scroll;
            if (actual < d->count) {
                uint32_t now = SDL_GetTicks();
                if (actual == d->last_click_idx &&
                    now - d->last_click_ms < DLG_DOUBLE_CLICK_MS) {
                    d->selected = actual;
                    open_selected(d);
                    return;
                }
                d->selected = actual;
                d->last_click_idx = actual;
                d->last_click_ms = now;
            }
        }
    }
}

void dialog_draw(FileDialog* d) {
    System* sys = d->sys;
    if (!sys || !sys->screen_pixels) return;
    int32_t w = sys->screen_width, h = sys->screen_height;
    int32_t dx = (w - DLG_W) / 2, dy = (h - DLG_H) / 2;

    // 1. Double-bordered main box
    dlg_fill_rect(sys, dx, dy, DLG_W, DLG_H, 0xFFFFFF);
    dlg_draw_rect(sys, dx, dy, DLG_W, DLG_H, 0x000000);
    dlg_draw_rect(sys, dx + 2, dy + 2, DLG_W - 4, DLG_H - 4, 0x000000);

    // 2. Floating title bar
    int32_t title_w = 160;
    int32_t tx = dx + (DLG_W - title_w) / 2, ty = dy - 12;
    dlg_fill_rect(sys, tx, ty, title_w, 24, 0xFFFFFF);
    dlg_draw_rect(sys, tx, ty, title_w, 24, 0x000000);
    dlg_draw_bitmap(sys, tx + 6, ty + 4, icon_floppy, 15, 0x000000);
    dlg_draw_str(sys, tx + 28, ty + 6, "System Startup", 0x000000);

    // 3. File list box + scrollbar
    int32_t lb_x = dx + 20, lb_y = dy + 40;
    dlg_draw_rect(sys, lb_x, lb_y, DLG_LIST_W, DLG_LIST_H, 0x000000);
    dlg_draw_rect(sys, lb_x + DLG_LIST_W - DLG_SB_W, lb_y, DLG_SB_W, DLG_LIST_H, 0x000000);
    dlg_draw_rect(sys, lb_x + DLG_LIST_W - DLG_SB_W, lb_y, DLG_SB_W, DLG_SB_W, 0x000000);
    dlg_draw_bitmap(sys, lb_x + DLG_LIST_W - DLG_SB_W + 4, lb_y + 7, icon_up_arrow, 5, 0x000000);
    dlg_draw_rect(sys, lb_x + DLG_LIST_W - DLG_SB_W, lb_y + DLG_LIST_H - DLG_SB_W, DLG_SB_W, DLG_SB_W, 0x000000);
    dlg_draw_bitmap(sys, lb_x + DLG_LIST_W - DLG_SB_W + 4, lb_y + DLG_LIST_H - DLG_SB_W + 7, icon_down_arrow, 5, 0x000000);

    // Entries
    int visible = DLG_LIST_H / DLG_ITEM_H;
    for (int i = 0; i < visible; i++) {
        int idx = i + d->scroll;
        if (idx >= d->count) break;
        DialogEntry* e = &d->entries[idx];

        uint32_t color = 0x000000;
        if (idx == d->selected) {
            dlg_fill_rect(sys, lb_x + 1, lb_y + 1 + i * DLG_ITEM_H,
                          DLG_LIST_W - DLG_SB_W - 1, DLG_ITEM_H - 1, 0x000000);
            color = 0xFFFFFF;
        }

        const uint16_t* icon = e->is_dir ? icon_folder : icon_doc;
        dlg_draw_bitmap(sys, lb_x + 5, lb_y + 4 + i * DLG_ITEM_H, icon, 11, color);
        dlg_draw_str(sys, lb_x + 25, lb_y + 4 + i * DLG_ITEM_H, e->name, color);
    }

    // 4. Buttons: Open (default, thick border) and Cancel
    int32_t bx = dx + DLG_W - 100, by = dy + DLG_H - 45;
    dlg_draw_rect(sys, bx - 2, by - 2, DLG_BTN_W + 4, DLG_BTN_H + 4, 0x000000);
    dlg_fill_rect(sys, bx, by, DLG_BTN_W, DLG_BTN_H, 0x000000);
    dlg_fill_rect(sys, bx + 1, by + 1, DLG_BTN_W - 2, DLG_BTN_H - 2, 0xEEEEEE);
    dlg_draw_str(sys, bx + 15, by + 7, "Open", 0x000000);

    bx -= 90;
    dlg_fill_rect(sys, bx, by, DLG_BTN_W, DLG_BTN_H, 0x000000);
    dlg_fill_rect(sys, bx + 1, by + 1, DLG_BTN_W - 2, DLG_BTN_H - 2, 0xEEEEEE);
    dlg_draw_str(sys, bx + 10, by + 7, "Cancel", 0x000000);
}
