#include "system.h"
#include "vfs.h"
#include "machine.h"
#include "chicago.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SCI_CMD_ADDR    (SCI_PORT + 4)
#define SCI_ARG1_ADDR   (SCI_PORT + 8)
#define SCI_ARG2_ADDR   (SCI_PORT + 12)
#define SCI_ARG3_ADDR   (DEVICE_MEMORY_OFFSET + 0x0124)

#define SCI_VFS_OPEN    10
#define SCI_VFS_CLOSE   11
#define SCI_VFS_READ    12
#define SCI_VFS_WRITE   13
#define SCI_VFS_BIND    14
#define SCI_VFS_SEEK    23
#define SCI_VFS_STAT    24
#define SCI_VFS_WRITE_CHUNK 25

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
    if (address == SCI_CMD_ADDR || address == SCI_ARG1_ADDR || address == SCI_ARG2_ADDR || address == SCI_ARG3_ADDR) {
        *success = true;
        return (int32_t)read_mem32(sys, address);
    }

    if (address == MOUSE_PORT) { *success = true; return sys->mouse_x; }
    if (address == MOUSE_PORT + 4) { *success = true; return sys->mouse_y; }
    if (address == MOUSE_PORT + 8) { *success = true; return sys->mouse_btn; }

    if (address == CONTROLLER_PORT) { *success = true; return 0; }
    if (address == CONTROLLER_PORT + 4) { *success = true; return 0; }
    if (address == CONTROLLER_PORT + 8) { *success = true; return 0; }

    // TIME::unix@ / date@ / time@ / milli@ at DATETIME_PORT+4/8/12/16.
    if (address == DATETIME_PORT || address == DATETIME_PORT + 4 ||
        address == DATETIME_PORT + 8 || address == DATETIME_PORT + 12 ||
        address == DATETIME_PORT + 16) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        *success = true;
        if (address == DATETIME_PORT || address == DATETIME_PORT + 4)
            return (int32_t)now;
        if (address == DATETIME_PORT + 8)
            return ((tm.tm_year + 1900) << 16) | ((tm.tm_mon + 1) << 8) | tm.tm_mday;
        if (address == DATETIME_PORT + 12)
            return (tm.tm_hour << 16) | (tm.tm_min << 8) | tm.tm_sec;
        return 0; // milli: not tracked
    }

    // Fallback: return mirrored memory for other device-region addresses.
    if (address >= DEVICE_MEMORY_OFFSET && address + 4 <= sys->memory_size) {
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
            const uint8_t* font = chicago12x12_cff;
            if (arg1 != 0 && (uint32_t)arg1 < sys->memory_size) {
                font = &sys->memory[arg1];
            }
            int scale = sys->font_size ? sys->font_size : 12;
            uint32_t color = sys->text_color ? sys->text_color : 0xFFFFFF;
            system_draw_cff(sys, font, ch, x, y, color, scale);
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
    
    if (address >= DEVICE_MEMORY_OFFSET && address < DEVICE_MEMORY_OFFSET + 0x1000) {
        uint32_t offset = address - DEVICE_MEMORY_OFFSET;
        if (offset % 16 == 0 && sys->set_vector != NULL) {
            int index = offset / 16;
            sys->set_vector(sys, index, value);
            return true;
        }
    }

    if (address == TEXT_PORT + 4) {
        sys->text_attr = value;
        return true;
    }

    // Audio control port: writing the sound ID triggers playback (matches Go behavior)
    if (address == AUDIO_PORT + 4) {
        if (sys->play_sound) sys->play_sound(value);
        return true;
    }
    if (address == TEXT_PORT + 8) {
        sys->text_cursor = value;
        return true;
    }

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
    
    // Defaults for screen
    sys->screen_width = 640;
    sys->screen_height = 480;
    sys->screen_pixels = (uint8_t*)malloc(sys->screen_width * sys->screen_height * 4);
    sys->back_pixels = (uint8_t*)malloc(sys->screen_width * sys->screen_height * 4);
    if (sys->screen_pixels) memset(sys->screen_pixels, 0, sys->screen_width * sys->screen_height * 4);
    if (sys->back_pixels) memset(sys->back_pixels, 0, sys->screen_width * sys->screen_height * 4);
    sys->play_sound = NULL;
    sys->font_id = 2;
    sys->font_size = 12;
    sys->text_color = 0xFFFFFF;
    sys->active_win_id = 1;
    sys->next_vm_id = 1;
    strncpy(sys->sandbox_root, ".", sizeof(sys->sandbox_root) - 1);

    return sys;
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
    if (sys->back_pixels) free(sys->back_pixels);
    free(sys);
}

void system_set_memory(System* sys, uint8_t* mem, uint32_t mem_size) {
    sys->memory = mem;
    sys->memory_size = mem_size;
    
    // Alias screen_pixels to the VideoFramebuffer region so VM memory writes automatically show up
    if (mem_size >= 0x200000) {
        sys->screen_pixels = &mem[0x100000];
    } else {
        sys->screen_pixels = NULL;
    }
}

void system_set_vector_callbacks(System* sys, uint32_t (*get)(System*, int), void (*set)(System*, int, uint32_t), void* vm) {
    sys->get_vector = get;
    sys->set_vector = set;
    sys->vm_ptr = vm;
}

void system_set_resolution(System* sys, int32_t width, int32_t height) {
    if (!sys) return;
    sys->screen_width = width;
    sys->screen_height = height;
    
    // NOTE: screen_pixels is usually aliased to vm memory by system_set_memory.
    // However, if it was allocated, we'd need to reallocate. 
    // Usually cloister will call this AFTER machine_create, so screen_pixels points to mem[0x100000].
    // The back_pixels must be reallocated.
    if (sys->back_pixels) {
        free(sys->back_pixels);
    }
    sys->back_pixels = (uint8_t*)calloc(1, width * height * 4);
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
    unsigned char* data = chicago12x12_cff;
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

    unsigned char* data = chicago12x12_cff;
    int width = data[(uint8_t)c];
    if (width == 0 && c != ' ') return 0;

    int tile_size = 16;
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

void system_draw_cff(System* sys, const uint8_t* font_data, char c, int32_t x, int32_t y, uint32_t color, int scale) {
    if (!sys || !font_data) return;
    if (font_data == chicago12x12_cff) {
        system_draw_char(sys, x, y, c, color, scale);
        return;
    }
    int32_t sw = sys->screen_width;
    int32_t sh = sys->screen_height;
    if (!sys->screen_pixels) return;

    int width = font_data[(uint8_t)c];
    if (width == 0 && c != ' ') return;

    int tile_size = 16;
    int tile_count = (tile_size / 8) * (tile_size / 8);
    int offset = 256 + (uint8_t)c * tile_count * 8;
    double sc = system_normalize_draw_scale(sys, scale);

    uint8_t a = 0xFF;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    int idx = 0;
    for (int tx = 0; tx < tile_size / 8; tx++) {
        for (int ty = 0; ty < tile_size / 8; ty++) {
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
