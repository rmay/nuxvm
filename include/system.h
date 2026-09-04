#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"
#include "vm.h"
#include "vfs.h"

/* Guest I/O is Plan 9 VFS files. This band is only the SCI trap that
 * implements open/read/write/close/bind — not device ports. */
#define SCI_PORT         (DEVICE_MEMORY_OFFSET + 0x00D0)
#define SCI_CMD_ADDR     (SCI_PORT + 4)
#define SCI_ARG1_ADDR    (SCI_PORT + 8)
#define SCI_ARG2_ADDR    (SCI_PORT + 12)
#define SCI_ARG3_ADDR    (DEVICE_MEMORY_OFFSET + 0x0124)

#define SYS_SNARF_MAX       65536
#define SYS_LAUNCH_MAX      256
#define SYS_MAX_CHILD_VMS   16
#define SYS_INPUT_QUEUE_SZ  64

/* DRAW::set-font ids. 0 and 1 share Chicago glyphs; 1 keeps the old 7x13 scale. */
#define FONT_CHICAGO 0
#define FONT_BASIC   1
#define FONT_GENEVA  2
#define FONT_MONACO  3

/* /dev/draw cmd 11 SetChan. 0 is RGB (default); k8/k2/k1 map ink to luma;
 * c4 snaps ink to the nearest of the 16 fixed system palette entries.
 * The palette is in src/system.c and mirrored in lib/draw.lux -- see
 * docs/palette.md. */
#define DRAW_CHAN_RGB 0
#define DRAW_CHAN_K8  1
#define DRAW_CHAN_K2  2
#define DRAW_CHAN_K1  3
#define DRAW_CHAN_C4  4

/* Entry i of the fixed 16-colour system palette, as 0xRRGGBB.
 * Out-of-range i returns 0. */
uint32_t system_palette_entry(int i);

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
    bool screen_pixels_owned; /* true if screen_pixels was malloc'd by System */
    uint8_t* back_pixels;
    uint32_t rng_state;

    int32_t sci_result;

    bool yielded;
    uint64_t frame_commits; // incremented by system_end_frame; diagnostic for NUXVM_MENU_DEBUG
    int32_t last_tick_cycles; // cycles executed by the most recent machine_tick; diagnostic

    uint32_t text_attr;
    uint32_t text_cursor;
    uint32_t text_color;
    uint8_t font_id;
    uint8_t font_size;
    uint8_t draw_chan; /* DRAW_CHAN_*: RGB / k8 / k2 / k1 */

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

    // When set, /dev/time reports time_ms as its monotonic-ms field instead of
    // CLOCK_MONOTONIC. Test-only: a headless harness runs machine_tick far
    // faster than real time, so a wall clock never advances a simulation step.
    bool time_frozen;
    uint32_t time_ms;

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
void system_set_sandbox_root(System* sys, const char* root);

void system_push_kbd_event(System* sys, uint8_t type, int32_t keycode, uint32_t modifiers);
void system_push_mouse_event(System* sys, uint8_t type, int32_t x, int32_t y, uint8_t btn);
// One host input event, fanned out to the legacy events[] ring and to the
// kbd/mouse queues. Types: 0 KEY_DOWN, 1 KEY_UP, 2 MOVE, 3 BUTTON_DOWN,
// 4 BUTTON_UP. data is a keycode for 0/1 and a packed (x<<12)|y for 2-4.
void system_push_host_event(System* sys, uint32_t type, uint32_t data, uint32_t mods);
// Pins /dev/time's monotonic-ms field so a harness can step the guest's
// fixed-timestep loop by hand. Once frozen it only moves when set again.
void system_freeze_monotonic_ms(System* sys, uint32_t ms);
void system_set_dialog_result(System* sys, const char* path);

uint32_t system_map_color(const System* sys, uint32_t color);
void system_fill_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void system_fill_pat(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, int pat);
void system_draw_rect(System* sys, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
int system_draw_char(System* sys, int32_t x, int32_t y, char c, uint32_t color, int scale);
void system_draw_text(System* sys, int32_t x, int32_t y, const char* str, uint32_t color, int scale);
void system_draw_text_len(System* sys, int32_t x, int32_t y, const char* str, int16_t len, uint32_t color, int scale);
void system_draw_cff(System* sys, const uint8_t* font_data, int nbytes, char c, int32_t x, int32_t y, uint32_t color, int scale);
void system_draw_tile(System* sys, const uint8_t* pixels, int size, int32_t x, int32_t y, int use_key, uint32_t key);
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