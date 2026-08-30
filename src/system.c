#include "system.h"
#include "vfs.h"
#include "machine.h"
#include "host_fonts.h"
#include "cff.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SCI_VFS_OPEN    10
#define SCI_VFS_CLOSE   11
#define SCI_VFS_READ    12
#define SCI_VFS_WRITE   13
#define SCI_VFS_BIND    14
#define SCI_VFS_SEEK    23
#define SCI_VFS_STAT    24
#define SCI_VFS_WRITE_CHUNK 25
#define SCI_VFS_READ_NOYIELD 26

#define SCI_CREATE_WIN  1
#define SCI_CLOSE_WIN   2
#define SCI_MOVE_WIN    3
#define SCI_DRAW_RECT   4
#define SCI_DRAW_TEXT   5
#define SCI_SET_PIXEL   6
#define SCI_GET_WIN_SIZE 7
#define SCI_FOCUS_WIN   8
#define SCI_POLL_EVENT  9

#define SCI_PLAY_SOUND      15
#define SCI_YIELD           16
#define SCI_GET_PID         17
#define SCI_GET_ACTIVE_WIN  18
#define SCI_DRAW_CFF        19
#define SCI_DEBUG_PRINT     20
#define SCI_OPEN_FILE_DIALOG 21
#define SCI_SET_WINDOW_TITLE 22

static uint32_t read_mem32(System* sys, uint32_t addr);

static void read_cstring(System* sys, uint32_t ptr, char* out, size_t outlen) {
    if (!out || outlen == 0) return;
    out[0] = '\0';
    if (!sys || ptr >= sys->memory_size) return;
    size_t i = 0;
    for (; i + 1 < outlen && ptr + i < sys->memory_size; i++) {
        char c = (char)sys->memory[ptr + i];
        if (c == '\0') break;
        out[i] = c;
    }
    out[i] = '\0';
}

static int32_t system_read(DeviceBus* bus, uint32_t address, bool* success) {
    System* sys = (System*)bus->user_data;

    if (address == SCI_PORT) {
        *success = true;
        return sys->sci_result;
    }
    if (address == SCI_CMD_ADDR || address == SCI_ARG1_ADDR ||
        address == SCI_ARG2_ADDR || address == SCI_ARG3_ADDR) {
        *success = true;
        return (int32_t)read_mem32(sys, address);
    }

    *success = false;
    return 0;
}

static uint32_t read_mem32(System* sys, uint32_t addr) {
    if (addr + 4 > sys->memory_size) return 0;
    uint32_t val = 0;
    val |= (uint32_t)sys->memory[addr] << 24;
    val |= (uint32_t)sys->memory[addr+1] << 16;
    val |= (uint32_t)sys->memory[addr+2] << 8;
    val |= (uint32_t)sys->memory[addr+3];
    return val;
}

static void handle_sci(System* sys) {
    int32_t cmd = read_mem32(sys, SCI_CMD_ADDR);
    int32_t arg1 = read_mem32(sys, SCI_ARG1_ADDR);
    int32_t arg2 = read_mem32(sys, SCI_ARG2_ADDR);
    int32_t arg3 = read_mem32(sys, SCI_ARG3_ADDR);

    sys->sci_result = 0;
    
    switch (cmd) {
        // --- Process & Debug ---
        case SCI_YIELD:
            sys->yielded = true;
            sys->sci_result = 0;
            break;
        case SCI_GET_PID:
            sys->sci_result = 1;
            break;
        case SCI_DEBUG_PRINT: {
            char msg[512];
            read_cstring(sys, (uint32_t)arg1, msg, sizeof(msg));
            fprintf(stderr, "[LUX-DEBUG] %s\n", msg);
            sys->sci_result = 0;
            break;
        }
        case SCI_SET_WINDOW_TITLE: {
            char title[256];
            read_cstring(sys, (uint32_t)arg1, title, sizeof(title));
            if (sys->set_window_title) {
                sys->set_window_title(sys->title_ctx, title);
            }
            sys->sci_result = 0;
            break;
        }
        case SCI_OPEN_FILE_DIALOG:
            if (sys->open_file_dialog) {
                sys->open_file_dialog(sys->dialog_ctx);
            }
            sys->sci_result = 0;
            break;

        // --- VFS ---
        case SCI_VFS_OPEN: {
            char path[256] = {0};
            uint32_t ptr = (uint32_t)arg1;
            for (int i = 0; i < 255; i++) {
                if (ptr >= sys->memory_size) break;
                char c = (char)sys->memory[ptr++];
                if (c == 0) break;
                path[i] = c;
            }
            path[255] = '\0';
            sys->sci_result = vfs_open(sys, path, arg2);
            break;
        }
        case SCI_VFS_CLOSE: {
            int res = vfs_close(sys, arg1);
            if (res != 0) sys->sci_result = -1;
            break;
        }
        case SCI_VFS_READ: {
            int32_t fd = (arg1 >> 16) & 0xFFFF;
            int32_t length = arg1 & 0xFFFF;
            if (arg3 != 0) {
                fd = arg1;
                length = arg3;
            }
            if (arg2 < 0 || (uint32_t)arg2 >= sys->memory_size || length < 0) {
                sys->sci_result = -1;
                break;
            }
            if ((uint32_t)length > sys->memory_size - (uint32_t)arg2) {
                length = (int32_t)(sys->memory_size - (uint32_t)arg2);
            }
            sys->sci_result = vfs_read(sys, fd, &sys->memory[arg2], length);
            break;
        }
        case SCI_VFS_READ_NOYIELD: {
            int32_t fd = (arg1 >> 16) & 0xFFFF;
            int32_t length = arg1 & 0xFFFF;
            if (arg3 != 0) {
                fd = arg1;
                length = arg3;
            }
            if (arg2 < 0 || (uint32_t)arg2 >= sys->memory_size || length < 0) {
                sys->sci_result = -1;
                break;
            }
            if ((uint32_t)length > sys->memory_size - (uint32_t)arg2) {
                length = (int32_t)(sys->memory_size - (uint32_t)arg2);
            }
            sys->sci_result = vfs_read_noyield(sys, fd, &sys->memory[arg2], length);
            break;
        }
        case SCI_VFS_WRITE: {
            int32_t fd = (arg1 >> 16) & 0xFFFF;
            int32_t length = arg1 & 0xFFFF;
            if (arg3 != 0) {
                fd = arg1;
                length = arg3;
            }
            if (arg2 < 0 || (uint32_t)arg2 >= sys->memory_size || length < 0) {
                sys->sci_result = -1;
                break;
            }
            if ((uint32_t)length > sys->memory_size - (uint32_t)arg2) {
                length = (int32_t)(sys->memory_size - (uint32_t)arg2);
            }
            sys->sci_result = vfs_write(sys, fd, &sys->memory[arg2], length);
            break;
        }
        case SCI_VFS_SEEK:
            sys->sci_result = (int32_t)vfs_seek(sys, arg1, (int64_t)arg2);
            break;
        case SCI_VFS_STAT:
            sys->sci_result = (int32_t)vfs_stat(sys, arg1);
            break;
        case SCI_VFS_BIND: {
            char mpath[256];
            read_cstring(sys, (uint32_t)arg2, mpath, sizeof(mpath));
            sys->sci_result = vfs_bind(sys, arg1, mpath);
            break;
        }
        case SCI_VFS_WRITE_CHUNK: {
            int32_t paramPtr = arg1;
            if (paramPtr < 0 || (uint32_t)paramPtr + 20 > sys->memory_size) {
                sys->sci_result = -1;
                break;
            }
            int32_t fd = read_mem32(sys, paramPtr);
            int32_t bufPtr = read_mem32(sys, paramPtr + 4);
            int32_t length = read_mem32(sys, paramPtr + 8);
            if (length <= 0 || bufPtr < 0 || (uint32_t)bufPtr + length > sys->memory_size) {
                sys->sci_result = -1;
                break;
            }
            sys->sci_result = vfs_write(sys, fd, &sys->memory[bufPtr], length);
            break;
        }

        // --- Graphics / Window ---
        case SCI_CREATE_WIN:
            if (sys->active_win_id == 0) sys->active_win_id = 1;
            sys->sci_result = sys->active_win_id;
            (void)arg1; (void)arg2;
            break;
        case SCI_CLOSE_WIN:
        case SCI_MOVE_WIN:
        case SCI_DRAW_RECT:
            sys->sci_result = 0;
            break;
        case SCI_DRAW_TEXT: {
            char text[1024];
            read_cstring(sys, (uint32_t)arg2, text, sizeof(text));
            int32_t cx = (int32_t)(sys->text_cursor & 0xFFFF);
            int32_t cy = (int32_t)((sys->text_cursor >> 16) & 0xFFFF);
            int scale = sys->font_size ? sys->font_size : 12;
            uint32_t color = sys->text_color ? sys->text_color : 0xFFFFFF;
            system_draw_text(sys, cx, cy, text, color, scale);
            sys->sci_result = 0;
            (void)arg1;
            break;
        }
        case SCI_SET_PIXEL: {
            int32_t x = arg2 & 0xFFFF;
            int32_t y = (arg2 >> 16) & 0xFFFF;
            system_set_pixel(sys, x, y, (uint32_t)arg3);
            sys->sci_result = 0;
            (void)arg1;
            break;
        }
        case SCI_DRAW_CFF: {
            char ch = (char)((uint32_t)arg2 >> 24);
            int32_t x = (int32_t)(((uint32_t)arg2 >> 12) & 0xFFF);
            int32_t y = (int32_t)((uint32_t)arg2 & 0xFFF);
            const uint8_t* builtin = host_font_chicago();
            const uint8_t* font = builtin;
            if (arg1 != 0 && (uint32_t)arg1 < sys->memory_size) {
                font = &sys->memory[arg1];
            }
            int scale = sys->font_size ? sys->font_size : 12;
            uint32_t color = sys->text_color ? sys->text_color : 0xFFFFFF;
            int nbytes = (builtin && font == builtin) ? (int)host_font_chicago_len() : CFF_LEN_UF2;
            system_draw_cff(sys, font, nbytes, ch, x, y, color, scale);
            sys->sci_result = 0;
            break;
        }
        case SCI_FOCUS_WIN:
            sys->active_win_id = arg1;
            sys->sci_result = 0;
            break;
        case SCI_GET_WIN_SIZE:
            sys->sci_result = (sys->screen_width << 16) | (sys->screen_height & 0xFFFF);
            (void)arg1;
            break;
        case SCI_GET_ACTIVE_WIN:
            sys->sci_result = sys->active_win_id;
            break;

        // --- Input (Stubs) ---
        case SCI_POLL_EVENT:
            if (sys->event_head != sys->event_tail) {
                sys->sci_result = sys->events[sys->event_head];
                sys->event_head = (sys->event_head + 1) % 64;
            } else {
                sys->sci_result = 0;
            }
            break;

        // --- Sound ---
        case SCI_PLAY_SOUND:
            if (sys->play_sound) sys->play_sound(arg1);
            sys->sci_result = 0;
            break;

        default:
            fprintf(stderr, "Unhandled SCI command %d\n", cmd);
            break;
    }
    
    // Clear arg3 after call (bounds-checked)
    if (SCI_ARG3_ADDR + 4 <= sys->memory_size) {
        sys->memory[SCI_ARG3_ADDR] = 0;
        sys->memory[SCI_ARG3_ADDR+1] = 0;
        sys->memory[SCI_ARG3_ADDR+2] = 0;
        sys->memory[SCI_ARG3_ADDR+3] = 0;
    }
    
    // Write sci_result back to SCI_PORT memory so LOADI can read it
    if (SCI_PORT + 4 <= sys->memory_size) {
        uint32_t res = (uint32_t)sys->sci_result;
        sys->memory[SCI_PORT] = (res >> 24) & 0xFF;
        sys->memory[SCI_PORT+1] = (res >> 16) & 0xFF;
        sys->memory[SCI_PORT+2] = (res >> 8) & 0xFF;
        sys->memory[SCI_PORT+3] = res & 0xFF;
    }
}

static bool system_write(DeviceBus* bus, uint32_t address, int32_t value) {
    System* sys = (System*)bus->user_data;
    (void)value;

    if (address == SCI_ARG2_ADDR) {
        handle_sci(sys);
        return true;
    }

    return false;
}

System* system_create(void) {
    System* sys = (System*)malloc(sizeof(System));
    if (!sys) return NULL;
    memset(sys, 0, sizeof(System));
    
    sys->bus.read = system_read;
    sys->bus.write = system_write;
    sys->bus.user_data = sys;
    sys->rng_state = 12345;
    
    /* Logical size only. Pixel buffers are allocated when something draws:
     * screen_pixels aliases guest RAM in system_set_memory (if the image is
     * large enough); back_pixels is allocated in system_set_resolution. */
    sys->screen_width = 640;
    sys->screen_height = 480;
    sys->play_sound = NULL;
    sys->font_id = FONT_CHICAGO;
    sys->font_size = 12;
    sys->text_color = 0xFFFFFF;
    sys->active_win_id = 1;
    sys->next_vm_id = 1;
    strncpy(sys->sandbox_root, ".", sizeof(sys->sandbox_root) - 1);

    return sys;
}

static void release_owned_screen_pixels(System* sys) {
    if (sys->screen_pixels_owned) {
        free(sys->screen_pixels);
        sys->screen_pixels_owned = false;
    }
    sys->screen_pixels = NULL;
}

void system_free(System* sys) {
    if (!sys) return;
    vfs_state_free(sys);
    for (int i = 0; i < SYS_MAX_CHILD_VMS; i++) {
        if (sys->child_vms[i]) {
            machine_free(sys->child_vms[i]);
            sys->child_vms[i] = NULL;
        }
    }
    free(sys->snarf_buf);
    release_owned_screen_pixels(sys);
    if (sys->back_pixels) free(sys->back_pixels);
    free(sys);
}

void system_set_memory(System* sys, uint8_t* mem, uint32_t mem_size) {
    sys->memory = mem;
    sys->memory_size = mem_size;

    /* Drop a System-owned buffer before aliasing into guest RAM (or
     * clearing). The aliased pointer is not freed in system_free. */
    release_owned_screen_pixels(sys);

    // Alias screen_pixels to the VideoFramebuffer region so VM memory writes automatically show up
    if (mem_size >= 0x200000) {
        sys->screen_pixels = &mem[0x100000];
    }
}

void system_set_resolution(System* sys, int32_t width, int32_t height) {
    if (!sys) return;
    sys->screen_width = width;
    sys->screen_height = height;

    /* Front buffer stays an alias of guest RAM (system_set_memory). Only the
     * host back buffer is allocated here — and only for a real canvas. */
    if (sys->back_pixels) {
        free(sys->back_pixels);
        sys->back_pixels = NULL;
    }
    if (width <= 0 || height <= 0) return;
    sys->back_pixels = (uint8_t*)calloc(1, (size_t)width * (size_t)height * 4);
}

// --- /sys/draw support (initial rect implementation) ---

void system_fill_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!sys) return;
    if (w <= 0 || h <= 0) return;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b = (color       ) & 0xFF;

    int32_t sw = sys->screen_width  ? sys->screen_width  : 960;
    int32_t sh = sys->screen_height ? sys->screen_height : 720;

    // Clip
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;

    if (x0 >= x1 || y0 >= y1) return;

    uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
    if (!fb) return;
    int stride = sw * 4;

    for (int32_t py = y0; py < y1; py++) {
        uint8_t* row = fb + (size_t)py * stride + (size_t)x0 * 4;
        for (int32_t px = x0; px < x1; px++) {
            // C-side VM framebuffer layout expected by cloister renderer:
            // [0]=?, [1]=R, [2]=G, [3]=B
            row[0] = 0xFF;
            row[1] = r;
            row[2] = g;
            row[3] = b;
            row += 4;
        }
    }
}

void system_fill_pat(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int pat) {
    if (!sys || w <= 0 || h <= 0) return;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >>  8) & 0xFF;
    uint8_t b = (color       ) & 0xFF;
    int32_t sw = sys->screen_width  ? sys->screen_width  : 960;
    int32_t sh = sys->screen_height ? sys->screen_height : 720;
    int32_t x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
    if (!fb) return;
    int stride = sw * 4;
    for (int32_t py = y0; py < y1; py++) {
        uint8_t* row = fb + (size_t)py * stride + (size_t)x0 * 4;
        for (int32_t px = x0; px < x1; px++) {
            int on;
            if (pat == 1) on = ((py & 1) == 0);
            else if (pat == 2) on = ((px & 1) == 0) && ((py & 1) == 0);
            else if (pat == 3) on = !((px & 1) && (py & 1));
            else on = (((px + py) & 1) == 0);
            if (on) {
                row[0] = 0xFF;
                row[1] = r;
                row[2] = g;
                row[3] = b;
            }
            row += 4;
        }
    }
}

void system_draw_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    // Outline using four 1-pixel thick fills (top, bottom, left, right)
    system_fill_rect(sys, x, y, w, 1, color);
    system_fill_rect(sys, x, y + h - 1, w, 1, color);
    system_fill_rect(sys, x, y, 1, h, color);
    system_fill_rect(sys, x + w - 1, y, 1, h, color);
}

const uint8_t* system_font_data_id(int font_id) {
    return host_font_data_id(font_id);
}

int system_font_nbytes_id(int font_id) {
    return host_font_nbytes_id(font_id);
}

const uint8_t* system_font_data(const System* sys) {
    return system_font_data_id(sys ? sys->font_id : FONT_CHICAGO);
}

int system_font_nbytes(const System* sys) {
    return system_font_nbytes_id(sys ? sys->font_id : FONT_CHICAGO);
}

double system_normalize_draw_scale(System* sys, int scale) {
    int raw = scale;
    if (raw == 0) {
        raw = sys->font_size ? sys->font_size : 12;
    }
    double sc = (double)raw;
    if (sys->font_id != 1 && raw < 6) {
        sc *= 16.0;
    }
    if (sys->font_id == 1) {
        if (sc >= 6.0) sc /= 12.0;
    } else {
        if (sc >= 6.0) sc /= 16.0;
    }
    if (sc <= 0.0) sc = 1.0;
    return sc;
}

int system_measure_char(System* sys, char c, int scale) {
    const uint8_t* data = system_font_data(sys);
    if (!data) return 0;
    int width = data[(uint8_t)c];
    if (width == 0) {
        if (c == ' ') width = 6;
        else return 0;
    }
    double sc = system_normalize_draw_scale(sys, scale);
    return (int)(width * sc);
}

int system_draw_char(System* sys, int32_t x, int32_t y, char c, uint32_t color, int scale) {
    if (!sys->screen_pixels) return 0;
    int32_t sw = sys->screen_width;
    int32_t sh = sys->screen_height;

    const uint8_t* data = system_font_data(sys);
    if (!data) return 0;
    int width = data[(uint8_t)c];
    if (width == 0 && c != ' ') return 0;

    int tile_size = cff_tile_size(system_font_nbytes(sys));
    if (tile_size <= 0) tile_size = 16;
    int num_v_tiles = tile_size / 8;
    int num_h_tiles = tile_size / 8;
    int tile_count = num_h_tiles * num_v_tiles;
    int offset = 256 + (uint8_t)c * tile_count * 8;

    double sc = system_normalize_draw_scale(sys, scale);

    uint8_t a = 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    int idx = 0;
    for (int tx = 0; tx < num_h_tiles; tx++) {
        for (int ty = 0; ty < num_v_tiles; ty++) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = data[offset + idx++];
                if (bits == 0) continue;

                double row_abs = (double)(ty * 8 + row);
                int start_y = (int)(row_abs * sc);
                int end_y = (int)((row_abs + 1.0) * sc);

                for (int py = start_y; py < end_y; py++) {
                    int pixel_y = y + py;
                    if (pixel_y < 0 || pixel_y >= sh) continue;

                    for (int col = 0; col < 8; col++) {
                        if ((bits & (0x80 >> col)) == 0) continue;

                        double col_abs = (double)(tx * 8 + col);
                        int start_x = (int)(col_abs * sc);
                        int end_x = (int)((col_abs + 1.0) * sc);

                        for (int px = start_x; px < end_x; px++) {
                            int pixel_x = x + px;
                            if (pixel_x < 0 || pixel_x >= sw) continue;

                            int src_idx = (pixel_y * sw + pixel_x) * 4;
                            uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
                            if (fb) {
                                fb[src_idx] = a;
                                fb[src_idx+1] = r;
                                fb[src_idx+2] = g;
                                fb[src_idx+3] = b;
                            }
                        }
                    }
                }
            }
        }
    }
    return system_measure_char(sys, c, scale);
}

void system_draw_text_len(System* sys, int32_t x, int32_t y, const char* str, int16_t len, uint32_t color, int scale) {
    int cur_x = x;
    for (int16_t i = 0; i < len; i++) {
        char c = str[i];
        int advance = system_draw_char(sys, cur_x, y, c, color, scale);
        if (advance == 0) advance = 6;
        cur_x += advance;
    }
}

void system_draw_text(System* sys, int32_t x, int32_t y, const char* str, uint32_t color, int scale) {
    int cur_x = x;
    while (*str) {
        char c = *str++;
        int advance = system_draw_char(sys, cur_x, y, c, color, scale);
        if (advance == 0) advance = 6;
        cur_x += advance;
    }
}

void system_begin_frame(System* sys) {
    // If we wanted to clear back_pixels we could, but typical nuxvm apps fill_rect anyway
    (void)sys;
}

void system_end_frame(System* sys) {
    if (sys->screen_pixels && sys->back_pixels) {
        memcpy(sys->screen_pixels, sys->back_pixels, (size_t)sys->screen_width * (size_t)sys->screen_height * 4);
    }
    sys->frame_commits++;
}

void system_set_sandbox_root(System* sys, const char* root) {
    if (!sys || !root) return;
    strncpy(sys->sandbox_root, root, sizeof(sys->sandbox_root) - 1);
    sys->sandbox_root[sizeof(sys->sandbox_root) - 1] = '\0';
}

void system_push_kbd_event(System* sys, uint8_t type, int32_t keycode, uint32_t modifiers) {
    if (!sys) return;
    int next = (sys->kbd_tail + 1) % SYS_INPUT_QUEUE_SZ;
    if (next == sys->kbd_head) return;
    SysInputEvent* e = &sys->kbd_queue[sys->kbd_tail];
    e->type = type;
    e->btn = 0;
    e->x_or_key = (uint16_t)(keycode & 0xFFFF);
    e->y = 0;
    e->modifiers = modifiers;
    sys->kbd_tail = next;
}

void system_push_mouse_event(System* sys, uint8_t type, int32_t x, int32_t y, uint8_t btn) {
    if (!sys) return;
    int next = (sys->mouse_tail + 1) % SYS_INPUT_QUEUE_SZ;
    if (next == sys->mouse_head) return;
    SysInputEvent* e = &sys->mouse_queue[sys->mouse_tail];
    e->type = type;
    e->btn = btn;
    e->x_or_key = (uint16_t)(x & 0xFFFF);
    e->y = (uint16_t)(y & 0xFFFF);
    e->modifiers = 0;
    sys->mouse_tail = next;
}

void system_set_dialog_result(System* sys, const char* path) {
    if (!sys || !path) return;
    strncpy(sys->dialog_result, path, sizeof(sys->dialog_result) - 1);
    sys->dialog_result[sizeof(sys->dialog_result) - 1] = '\0';
    sys->dialog_ready = true;
}

void system_set_pixel(System* sys, int32_t x, int32_t y, uint32_t color) {
    if (!sys) return;
    int32_t sw = sys->screen_width ? sys->screen_width : 960;
    int32_t sh = sys->screen_height ? sys->screen_height : 720;
    if (x < 0 || y < 0 || x >= sw || y >= sh) return;
    uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
    if (!fb) return;
    int idx = (y * sw + x) * 4;
    fb[idx + 0] = 0xFF;
    fb[idx + 1] = (color >> 16) & 0xFF;
    fb[idx + 2] = (color >> 8) & 0xFF;
    fb[idx + 3] = color & 0xFF;
}

void system_draw_cff(System* sys, const uint8_t* font_data, int nbytes, char c, int32_t x, int32_t y, uint32_t color, int scale) {
    if (!sys || !font_data) return;
    if (nbytes <= 0 && font_data && font_data == host_font_chicago()) {
        nbytes = (int)host_font_chicago_len();
    }
    int tile_size = cff_tile_size(nbytes);
    if (tile_size <= 0) return;

    int32_t sw = sys->screen_width;
    int32_t sh = sys->screen_height;
    if (!sys->screen_pixels) return;

    int width = font_data[(uint8_t)c];
    if (width == 0 && c != ' ') return;

    int num_h = tile_size / 8;
    int num_v = tile_size / 8;
    int tile_count = num_h * num_v;
    int offset = 256 + (uint8_t)c * tile_count * 8;
    if (offset < 256 || offset + tile_count * 8 > nbytes) return;

    double sc;
    if (scale < 6) {
        sc = (scale <= 0) ? 1.0 : (double)scale;
    } else {
        sc = system_normalize_draw_scale(sys, scale);
    }
    if (sc <= 0.0) sc = 1.0;

    uint8_t a = 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    int idx = 0;
    for (int tx = 0; tx < num_h; tx++) {
        for (int ty = 0; ty < num_v; ty++) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = font_data[offset + idx++];
                if (bits == 0) continue;
                for (int py = (int)((ty * 8 + row) * sc); py < (int)((ty * 8 + row + 1) * sc); py++) {
                    int pixel_y = y + py;
                    if (pixel_y < 0 || pixel_y >= sh) continue;
                    for (int col = 0; col < 8; col++) {
                        if ((bits & (0x80 >> col)) == 0) continue;
                        for (int px = (int)((tx * 8 + col) * sc); px < (int)((tx * 8 + col + 1) * sc); px++) {
                            int pixel_x = x + px;
                            if (pixel_x < 0 || pixel_x >= sw) continue;
                            int src_idx = (pixel_y * sw + pixel_x) * 4;
                            uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
                            if (fb) {
                                fb[src_idx] = a;
                                fb[src_idx+1] = r;
                                fb[src_idx+2] = g;
                                fb[src_idx+3] = b;
                            }
                        }
                    }
                }
            }
        }
    }
    (void)width;
}

void system_draw_tile(System* sys, const uint8_t* pixels, int size, int32_t x, int32_t y, int use_key, uint32_t key) {
    if (!sys || !pixels || size <= 0) return;
    if (!sys->screen_pixels) return;

    int32_t sw = sys->screen_width;
    int32_t sh = sys->screen_height;

    uint8_t kr = (key >> 16) & 0xFF;
    uint8_t kg = (key >> 8) & 0xFF;
    uint8_t kb = key & 0xFF;

    uint8_t* fb = sys->back_pixels ? sys->back_pixels : sys->screen_pixels;
    if (!fb) return;

    for (int py = 0; py < size; py++) {
        int pixel_y = y + py;
        if (pixel_y < 0 || pixel_y >= sh) continue;
        for (int px = 0; px < size; px++) {
            int pixel_x = x + px;
            if (pixel_x < 0 || pixel_x >= sw) continue;
            int src = (py * size + px) * 3;
            uint8_t r = pixels[src];
            uint8_t g = pixels[src + 1];
            uint8_t b = pixels[src + 2];
            if (use_key && r == kr && g == kg && b == kb) continue;
            int idx = (pixel_y * sw + pixel_x) * 4;
            fb[idx + 0] = 0xFF;
            fb[idx + 1] = r;
            fb[idx + 2] = g;
            fb[idx + 3] = b;
        }
    }
}
