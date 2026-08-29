#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"
#include "vm.h"
#include "vfs.h"

#define SCREEN_PORT      (DEVICE_MEMORY_OFFSET + 0x0020)
#define AUDIO_PORT       (DEVICE_MEMORY_OFFSET + 0x0030)
#define CONTROLLER_PORT  (DEVICE_MEMORY_OFFSET + 0x0040)
#define MOUSE_PORT       (DEVICE_MEMORY_OFFSET + 0x0050)
#define FILE_PORT        (DEVICE_MEMORY_OFFSET + 0x0060)
#define DATETIME_PORT    (DEVICE_MEMORY_OFFSET + 0x0070)
#define RNG_PORT         (DEVICE_MEMORY_OFFSET + 0x0080)
#define TEXT_PORT        (DEVICE_MEMORY_OFFSET + 0x0090)
#define WINDOW_PORT      (DEVICE_MEMORY_OFFSET + 0x00B0)
#define WHEEL_PORT       (DEVICE_MEMORY_OFFSET + 0x00A0)
#define RESIZE_PORT      (DEVICE_MEMORY_OFFSET + 0x00C0)
#define PANE_PORT        (DEVICE_MEMORY_OFFSET + 0x00E0)
#define SCI_PORT         (DEVICE_MEMORY_OFFSET + 0x00D0)
#define MENU_PORT        (DEVICE_MEMORY_OFFSET + 0x00F0)
#define GPU_PORT         (DEVICE_MEMORY_OFFSET + 0x0100)

#define SYS_SNARF_MAX       65536
#define SYS_LAUNCH_MAX      256
#define SYS_MAX_CHILD_VMS   16
#define SYS_INPUT_QUEUE_SZ  64

/* DRAW::set-font ids. 0 and 1 share Chicago glyphs; 1 keeps the old 7x13 scale. */
#define FONT_CHICAGO 0
#define FONT_BASIC   1
#define FONT_GENEVA  2
#define FONT_MONACO  3

typedef struct Machine Machine;

typedef struct {
    uint8_t type;
    uint8_t btn;
    uint16_t x_or_key;
    uint16_t y;
    uint32_t modifiers;
} SysInputEvent;

typedef struct System {
    DeviceBus bus;
    uint8_t* memory;
    uint32_t memory_size;

    int32_t screen_width;
    int32_t screen_height;
    uint8_t* screen_pixels;
    uint8_t* back_pixels;
    uint32_t rng_state;

    int32_t sci_result;

    uint32_t (*get_vector)(struct System* sys, int index);
    void (*set_vector)(struct System* sys, int index, uint32_t addr);
    void* vm_ptr;

    bool yielded;

    uint32_t text_attr;
    uint32_t text_cursor;
    uint32_t text_color;
    uint8_t font_id;
    uint8_t font_size;

    int32_t mouse_x;
    int32_t mouse_y;
    uint32_t mouse_btn;
    int32_t wheel_y;

    uint32_t events[64];
    int event_head;
    int event_tail;

    SysInputEvent kbd_queue[SYS_INPUT_QUEUE_SZ];
    int kbd_head;
    int kbd_tail;

    SysInputEvent mouse_queue[SYS_INPUT_QUEUE_SZ];
    int mouse_head;
    int mouse_tail;

    uint8_t* snarf_buf;
    int snarf_len;
    int snarf_cap;

    char sandbox_root[512];

    int32_t active_win_id;
    int32_t next_vm_id;
    Machine* child_vms[SYS_MAX_CHILD_VMS];

    char dialog_result[512];
    bool dialog_ready;

    /* Path written to /sys/launch. Cloister copies this on HALT and
       loads that ROM in place of the current one (picker -> app). */
    char launch_path[SYS_LAUNCH_MAX];

    VFSState vfs;

    void (*play_sound)(int32_t sound_id);
    void (*set_window_title)(void* ctx, const char* title);
    void* title_ctx;
    void (*open_file_dialog)(void* ctx);
    void* dialog_ctx;
} System;

System* system_create(void);
void system_free(System* sys);
void system_set_memory(System* sys, uint8_t* mem, uint32_t mem_size);
void system_set_resolution(System* sys, int32_t width, int32_t height);
void system_set_vector_callbacks(System* sys, uint32_t (*get)(System*, int), void (*set)(System*, int, uint32_t), void* vm);
void system_set_sandbox_root(System* sys, const char* root);

void system_push_kbd_event(System* sys, uint8_t type, int32_t keycode, uint32_t modifiers);
void system_push_mouse_event(System* sys, uint8_t type, int32_t x, int32_t y, uint8_t btn);
void system_set_dialog_result(System* sys, const char* path);

void system_fill_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void system_fill_pat(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int pat);
void system_draw_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
int system_draw_char(System* sys, int32_t x, int32_t y, char c, uint32_t color, int scale);
void system_draw_text(System* sys, int32_t x, int32_t y, const char* str, uint32_t color, int scale);
void system_draw_text_len(System* sys, int32_t x, int32_t y, const char* str, int16_t len, uint32_t color, int scale);
void system_draw_cff(System* sys, const uint8_t* font_data, int nbytes, char c, int32_t x, int32_t y, uint32_t color, int scale);
void system_set_pixel(System* sys, int32_t x, int32_t y, uint32_t color);
double system_normalize_draw_scale(System* sys, int scale);
int system_measure_char(System* sys, char c, int scale);
const uint8_t* system_font_data_id(int font_id);
int system_font_nbytes_id(int font_id);
const uint8_t* system_font_data(const System* sys);
int system_font_nbytes(const System* sys);
void system_begin_frame(System* sys);
void system_end_frame(System* sys);

#endif // SYSTEM_H