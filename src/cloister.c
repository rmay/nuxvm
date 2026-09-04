#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>

#include "machine.h"
#include "vfs.h"
#include "compiler.h"
#include "dialog.h"
#include "system.h"
#include "rom.h"

#define DEFAULT_WIN_WIDTH 960
#define DEFAULT_WIN_HEIGHT 720
#define PICKER_PATH "apps/Picker.lux"

// Window size is resolved per app: a --width/--height CLI flag wins outright
// (and stays fixed for the whole session); otherwise an app that declares its
// own "@WIN_W <n> ;" / "@WIN_H <n> ;" constants (see apps/Easel.lux etc.) gets
// a window sized to fit its own layout instead of the one-size-fits-all
// default below, which apps that don't declare these keep.
static int g_win_w = DEFAULT_WIN_WIDTH;
static int g_win_h = DEFAULT_WIN_HEIGHT;
static bool g_cli_w_set = false;
static bool g_cli_h_set = false;
static int g_cli_w = DEFAULT_WIN_WIDTH;
static int g_cli_h = DEFAULT_WIN_HEIGHT;

// Scans a .lux source file's text for top-level "@WIN_W <int> ;" and
// "@WIN_H <int> ;" constant declarations (compiled .bin files carry no such
// metadata, so this is read straight from source before compiling). Matches
// on a word boundary so "@WIN_WIDTH" etc. can't be mistaken for "@WIN_W".
// Apps are normally launched by their compiled .bin (see the doc comments at
// the top of apps/*.lux), so a ".bin" path is redirected to its sibling
// ".lux" source (same directory, same basename) when one exists.
static bool scan_lux_win_size(const char* path, int* out_w, int* out_h) {
    size_t path_len = strlen(path);
    char lux_path[1024];
    const char* src_path;
    if (path_len >= 4 && strcmp(path + path_len - 4, ".lux") == 0) {
        src_path = path;
    } else if (path_len >= 4 && strcmp(path + path_len - 4, ".bin") == 0 &&
               path_len - 4 < sizeof(lux_path) - 4) {
        memcpy(lux_path, path, path_len - 4);
        strcpy(lux_path + path_len - 4, ".lux");
        src_path = lux_path;
    } else {
        return false;
    }

    FILE* f = fopen(src_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return false; }
    char* buf = malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return false; }
    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[nread] = '\0';

    int found_w = 0, found_h = 0;
    const char* p = buf;
    while ((p = strstr(p, "@WIN_")) != NULL) {
        const char* tag = p + 5;
        int is_w = strncmp(tag, "W", 1) == 0 && !isalnum((unsigned char)tag[1]) && tag[1] != '_';
        int is_h = strncmp(tag, "H", 1) == 0 && !isalnum((unsigned char)tag[1]) && tag[1] != '_';
        if (is_w || is_h) {
            const char* num = tag + 1;
            while (*num && isspace((unsigned char)*num)) num++;
            char* end;
            long v = strtol(num, &end, 10);
            if (end != num && v >= 100 && v <= 4000) {
                if (is_w) { found_w = (int)v; }
                else { found_h = (int)v; }
            }
        }
        p += 5;
    }
    free(buf);

    if (found_w > 0 && found_h > 0) {
        *out_w = found_w;
        *out_h = found_h;
        return true;
    }
    return false;
}

// Resolves the window size to use for launching `path`: a locked CLI
// override wins per-dimension, otherwise the app's own @WIN_W/@WIN_H (if
// declared), otherwise the shared default.
static void resolve_win_size(const char* path, int* out_w, int* out_h) {
    int scanned_w = 0, scanned_h = 0;
    bool scanned = scan_lux_win_size(path, &scanned_w, &scanned_h);
    *out_w = g_cli_w_set ? g_cli_w : (scanned ? scanned_w : DEFAULT_WIN_WIDTH);
    *out_h = g_cli_h_set ? g_cli_h : (scanned ? scanned_h : DEFAULT_WIN_HEIGHT);
}

// Frame timing diagnostics: set NUXVM_FRAME_DEBUG=1 to log per-phase
// frame time (event poll / VM tick / render+present) once a second.
static bool g_frame_debug = false;
typedef struct {
    double sum_ms;
    double max_ms;
} PhaseStat;
static void phase_stat_add(PhaseStat* s, double ms) {
    s->sum_ms += ms;
    if (ms > s->max_ms) s->max_ms = ms;
}
static void phase_stat_reset(PhaseStat* s) { s->sum_ms = 0.0; s->max_ms = 0.0; }

// Menu-hover latency diagnostics: set NUXVM_MENU_DEBUG=1 to log, with a shared
// timestamp clock, every raw mouse-motion event alongside every change to
// lib/ui.lux's MB_HOVER cell (address 0x8E0F60), so the two logs can be
// eyeballed together to see how many ms/frames separate an input from the
// menu library actually updating its hover state.
#define MB_HOVER_ADDR 0x8E0F60u
static bool g_menu_debug = false;
static int32_t read_le32(const uint8_t* mem, size_t mem_size, uint32_t addr) {
    if ((size_t)addr + 4 > mem_size) return 0;
    return (int32_t)((uint32_t)mem[addr] | ((uint32_t)mem[addr+1] << 8) |
                      ((uint32_t)mem[addr+2] << 16) | ((uint32_t)mem[addr+3] << 24));
}

// Audio handling
#define AUDIO_SAMPLE_RATE 44100
static SDL_AudioDeviceID audio_device;
static int32_t current_sound_id = 0;
static uint32_t audio_samples_played = 0;
static uint32_t audio_duration_samples = 0;

static void cloister_play_sound(int32_t sound_id) {
    current_sound_id = sound_id;
    audio_samples_played = 0;
    audio_duration_samples = (uint32_t)(0.12 * AUDIO_SAMPLE_RATE);
}

static void audio_callback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    int16_t* buffer = (int16_t*)stream;
    int samples = len / 2;

    if (current_sound_id == 0 || audio_samples_played >= audio_duration_samples) {
        memset(stream, 0, len);
        return;
    }

    double freq = (double)current_sound_id;
    double duration = 0.12;
    double fade = 0.02;

    for (int i = 0; i < samples; i++) {
        if (audio_samples_played >= audio_duration_samples) {
            buffer[i] = 0;
            continue;
        }

        double t = (double)audio_samples_played / (double)AUDIO_SAMPLE_RATE;
        double env = 1.0;
        if (t < fade) env = t / fade;
        else if (t > duration - fade) env = (duration - t) / fade;

        buffer[i] = (int16_t)(env * 0.25 * 32767.0 * sin(2.0 * M_PI * freq * t));
        audio_samples_played++;
    }
}

static void queue_event(Machine* m, uint32_t type, uint32_t data, uint32_t mods) {
    if (!m || !m->system) return;
    system_push_host_event(m->system, type, data, mods);
}

static uint32_t current_modifiers(SDL_Keymod m) {
    uint32_t mods = 0;
    if (m & KMOD_SHIFT) mods |= 1;
    if (m & KMOD_CTRL)  mods |= 2;
    if (m & KMOD_ALT)   mods |= 4;
    if (m & KMOD_GUI)   mods |= 8;
    return mods;
}

/* translate_key() has no case for bare modifier keysyms (they carry no
 * character), so pressing/releasing Shift by itself with no other key never
 * reached the app -- apps that read kbd-mods to modify a live mouse drag
 * (e.g. Easel's Shift-to-square) never saw the flag change. This sentinel
 * keycode is outside the printable range and every special-cased key the
 * apps compare against, so it safely does nothing except carry updated
 * mods through on-kbd. */
#define MOD_ONLY_KEYCODE 0xFF

static bool is_shift_keysym(SDL_Keycode k) {
    return k == SDLK_LSHIFT || k == SDLK_RSHIFT;
}

/* Tracks the mods last sent via MOD_ONLY_KEYCODE so a run of KEYDOWN/KEYUP
 * for the same held Shift key (SDL delivers these on modifier auto-repeat
 * on some keyboards/drivers) doesn't queue a fresh event every time -- only
 * an actual modifier-state change is worth telling the guest about. Without
 * this, a held Shift during a drag could flood the 64-slot event queue and
 * every kbd event drained still costs guest-side dispatch, which is what
 * made menus/palette feel laggy even though per-frame timing looked clean. */
static uint32_t g_last_mod_only_mods = 0xFFFFFFFFu;

static bool translate_key(SDL_Keycode k, bool shift, int32_t* out) {
    switch (k) {
        case SDLK_UP:        *out = 17; return true;
        case SDLK_DOWN:      *out = 18; return true;
        case SDLK_LEFT:      *out = 19; return true;
        case SDLK_RIGHT:     *out = 20; return true;
        case SDLK_PAGEUP:    *out = 21; return true;
        case SDLK_PAGEDOWN:  *out = 22; return true;
        case SDLK_HOME:      *out = 23; return true;
        case SDLK_END:       *out = 24; return true;
        case SDLK_SPACE:     *out = 32; return true;
        case SDLK_TAB:       *out = 9;  return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  *out = 13; return true;
        case SDLK_ESCAPE:    *out = 27; return true;
        case SDLK_BACKSPACE:
        case SDLK_DELETE:    *out = 8;  return true;
        default: break;
    }

    if (k >= SDLK_a && k <= SDLK_z) {
        *out = shift ? ('A' + (k - SDLK_a)) : k;
        return true;
    }
    if (k >= SDLK_0 && k <= SDLK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        *out = shift ? shifted[k - SDLK_0] : k;
        return true;
    }

    switch (k) {
        case '-':  *out = shift ? '_' : k; return true;
        case '=':  *out = shift ? '+' : k; return true;
        case '[':  *out = shift ? '{' : k; return true;
        case ']':  *out = shift ? '}' : k; return true;
        case '\\': *out = shift ? '|' : k; return true;
        case ';':  *out = shift ? ':' : k; return true;
        case '\'': *out = shift ? '"' : k; return true;
        case ',':  *out = shift ? '<' : k; return true;
        case '.':  *out = shift ? '>' : k; return true;
        case '/':  *out = shift ? '?' : k; return true;
        case '`':  *out = shift ? '~' : k; return true;
        default: break;
    }

    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) {
        *out = '1' + (k - SDLK_KP_1);
        return true;
    }
    if (k == SDLK_KP_0) {
        *out = '0';
        return true;
    }
    return false;
}

static void cloister_set_title(void* ctx, const char* title) {
    SDL_SetWindowTitle((SDL_Window*)ctx, title);
}

static FileDialog g_dialog;

static void cloister_open_dialog(void* ctx) {
    FileDialog* d = (FileDialog*)ctx;
    dialog_open(d, d->sys);
}

static bool load_app(const char* path, uint8_t** program_out, Machine** machine_out,
                     SDL_Window* win, int win_w, int win_h) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fclose(f);
        fprintf(stderr, "Empty or unreadable file: %s\n", path);
        return false;
    }

    char* file_content = malloc((size_t)fsize + 1);
    if (!file_content) {
        fclose(f);
        return false;
    }
    if (fread(file_content, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(file_content);
        fclose(f);
        fprintf(stderr, "Failed to read %s\n", path);
        return false;
    }
    file_content[fsize] = '\0';
    fclose(f);

    uint8_t* program = NULL;
    size_t code_len = 0;
    size_t path_len = strlen(path);
    bool is_lux = path_len >= 4 && strcmp(path + path_len - 4, ".lux") == 0;

    if (is_lux) {
        program = compile_source(file_content, GRAPHICAL_BASE_ADDRESS, &code_len, false);
        free(file_content);
        if (!program) {
            fprintf(stderr, "Failed to compile %s\n", path);
            return false;
        }
    } else {
        char romerr[256];
        program = rom_open_image((uint8_t*)file_content, (size_t)fsize, &code_len,
                                 NULL, NULL, NULL, romerr, sizeof(romerr));
        free(file_content);
        if (!program) {
            fprintf(stderr, "cloister: %s: %s\n", path, romerr);
            return false;
        }
    }

    Machine* machine = machine_create(program, (uint32_t)code_len, GRAPHICAL_BASE_ADDRESS,
                                      nux_guest_memory_size(GRAPHICAL_BASE_ADDRESS, (uint32_t)code_len), false);
    if (!machine) {
        free(program);
        fprintf(stderr, "Failed to create machine for %s\n", path);
        return false;
    }

    if (machine->system) {
        system_set_resolution(machine->system, win_w, win_h);
        machine->system->play_sound = cloister_play_sound;
        machine->system->set_window_title = cloister_set_title;
        machine->system->title_ctx = win;
        machine->system->open_file_dialog = cloister_open_dialog;
        machine->system->dialog_ctx = &g_dialog;
        g_dialog.sys = machine->system;
    }

    if (*machine_out) machine_free(*machine_out);
    if (*program_out) free(*program_out);
    *machine_out = machine;
    *program_out = program;
    return true;
}

// Loads `path`, resizing the OS window/texture first if its resolved size
// (CLI override, else its own @WIN_W/@WIN_H, else the default) differs from
// the currently open window -- used both for the initial launch and for
// in-session app switches (Picker -> app, app -> Picker, app -> app).
static bool switch_to_app(const char* path, SDL_Window* win, SDL_Renderer* ren, SDL_Texture** tex,
                          uint8_t** program_out, Machine** machine_out) {
    int new_w, new_h;
    resolve_win_size(path, &new_w, &new_h);
    if (new_w != g_win_w || new_h != g_win_h) {
        g_win_w = new_w;
        g_win_h = new_h;
        SDL_SetWindowSize(win, g_win_w, g_win_h);
        SDL_DestroyTexture(*tex);
        *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, g_win_w, g_win_h);
    }
    return load_app(path, program_out, machine_out, win, g_win_w, g_win_h);
}

static bool launch_path_ok(const char* p) {
    if (!p || !p[0]) return false;
    if (p[0] == '/') return false;
    if (strstr(p, "..") != NULL) return false;
    return true;
}

static void eject_rom(Machine** machine, uint8_t** program, SDL_Window* win) {
    dialog_free(&g_dialog);
    g_dialog.sys = NULL;
    if (*machine) {
        machine_free(*machine);
        *machine = NULL;
    }
    if (*program) {
        free(*program);
        *program = NULL;
    }
    SDL_SetWindowTitle(win, "Cloister");
}

int main(int argc, char** argv) {
    // Parse the optional ROM path plus --width/--height overrides. Order is
    // not significant: `cloister apps/Easel.bin --width 800` and
    // `cloister --width 800 apps/Easel.bin` both work.
    const char* app_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            g_cli_w = atoi(argv[++i]);
            g_cli_w_set = true;
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            g_cli_h = atoi(argv[++i]);
            g_cli_h_set = true;
        } else if (!app_path) {
            app_path = argv[i];
        }
    }
    bool from_argv = app_path != NULL;
    resolve_win_size(from_argv ? app_path : PICKER_PATH, &g_win_w, &g_win_h);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("Cloister", 100, 100, g_win_w, g_win_h, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Vsync is off by default: on macOS, SDL_RENDERER_PRESENTVSYNC (via the Metal
    // backend) buffers multiple frames deep, adding 2-3 frames (~30-50ms) of
    // input-to-display latency -- this is what made menu highlights visibly lag
    // behind the mouse. We pace frames manually instead (see FRAME_TARGET_MS
    // below) to keep CPU usage bounded without paying that latency cost.
    // Set NUXVM_VSYNC=1 to opt back into vsync (smoother/tear-free, higher latency).
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (getenv("NUXVM_VSYNC")) renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, renderer_flags);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, g_win_w, g_win_h);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 0);

    Machine* machine = NULL;
    uint8_t* program = NULL;
    bool running_rom = false;
    bool is_picker = false;

    if (from_argv) {
        if (!load_app(app_path, &program, &machine, win, g_win_w, g_win_h)) return 1;
        running_rom = true;
    } else {
        if (!load_app(PICKER_PATH, &program, &machine, win, g_win_w, g_win_h)) return 1;
        running_rom = true;
        is_picker = true;
    }

    g_frame_debug = getenv("NUXVM_FRAME_DEBUG") != NULL;
    g_menu_debug = getenv("NUXVM_MENU_DEBUG") != NULL;
    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 t_program_start = SDL_GetPerformanceCounter();
    PhaseStat stat_poll = {0}, stat_tick = {0}, stat_render = {0}, stat_total = {0};
    int frame_debug_count = 0;
    Uint64 frame_debug_window_start = SDL_GetPerformanceCounter();
    int32_t last_hover = INT32_MIN;
    uint64_t last_frame_commits = 0;
    uint64_t host_loop_iters = 0;
    Uint64 t_last_present = SDL_GetPerformanceCounter();

    const double FRAME_TARGET_MS = 1000.0 / 60.0;

    bool quit = false;
    while (!quit) {
        host_loop_iters++;
        Uint64 t_frame_start = SDL_GetPerformanceCounter();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (running_rom && g_dialog.active) {
                if (e.type == SDL_KEYDOWN) {
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        dialog_key(&g_dialog, code);
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                    dialog_mouse_down(&g_dialog, e.button.x, e.button.y);
                } else if (e.type == SDL_MOUSEWHEEL) {
                    dialog_wheel(&g_dialog, e.wheel.y);
                }
            } else if (machine) {
                if (e.type == SDL_KEYDOWN) {
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        uint32_t mods = current_modifiers((SDL_Keymod)e.key.keysym.mod);
                        queue_event(machine, 0, (uint32_t)code & 0xFFFFFF, mods);
                    } else if (is_shift_keysym(e.key.keysym.sym) && !e.key.repeat) {
                        uint32_t mods = current_modifiers(SDL_GetModState());
                        if (mods != g_last_mod_only_mods) {
                            queue_event(machine, 0, MOD_ONLY_KEYCODE, mods);
                            g_last_mod_only_mods = mods;
                        }
                    }
                } else if (e.type == SDL_KEYUP) {
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        queue_event(machine, 1, (uint32_t)code & 0xFFFFFF, 0);
                    } else if (is_shift_keysym(e.key.keysym.sym)) {
                        uint32_t mods = current_modifiers(SDL_GetModState());
                        if (mods != g_last_mod_only_mods) {
                            queue_event(machine, 0, MOD_ONLY_KEYCODE, mods);
                            g_last_mod_only_mods = mods;
                        }
                    }
                } else if (e.type == SDL_MOUSEMOTION) {
                    machine->system->mouse_x = e.motion.x;
                    machine->system->mouse_y = e.motion.y;
                    if (g_menu_debug) {
                        double t_ms = (double)(SDL_GetPerformanceCounter() - t_program_start) * 1000.0 / (double)perf_freq;
                        fprintf(stderr, "[menu] t=%.1f mousemotion x=%d y=%d\n", t_ms, e.motion.x, e.motion.y);
                    }
                    queue_event(machine, 2, (e.motion.x << 12) | (e.motion.y & 0xFFF), 0);
                } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                    uint32_t mods = current_modifiers(SDL_GetModState());
                    uint8_t btn = 1;
                    if (e.button.button == SDL_BUTTON_MIDDLE) btn = 2;
                    else if (e.button.button == SDL_BUTTON_RIGHT) btn = 3;
                    else if (e.button.button == SDL_BUTTON_LEFT && (mods & 2)) btn = 3;
                    machine->system->mouse_btn |= (1u << (btn - 1));
                    queue_event(machine, 3, (e.button.x << 12) | (e.button.y & 0xFFF), btn);
                } else if (e.type == SDL_MOUSEBUTTONUP) {
                    uint32_t mods = current_modifiers(SDL_GetModState());
                    uint8_t btn = 1;
                    if (e.button.button == SDL_BUTTON_MIDDLE) btn = 2;
                    else if (e.button.button == SDL_BUTTON_RIGHT) btn = 3;
                    else if (e.button.button == SDL_BUTTON_LEFT && (mods & 2)) btn = 3;
                    machine->system->mouse_btn &= ~(1u << (btn - 1));
                    queue_event(machine, 4, (e.button.x << 12) | (e.button.y & 0xFFF), btn);
                }
            }
        }

        Uint64 t_after_poll = g_frame_debug ? SDL_GetPerformanceCounter() : 0;

        // Set when machine_tick returned because it burned its per-tick cycle
        // budget rather than because the app finished a frame and yielded. The
        // guest is then stopped mid-frame with a half-drawn back buffer.
        bool vm_mid_frame = false;

        if (running_rom && machine) {
            if (!machine_tick(machine)) {
                char launch[SYS_LAUNCH_MAX];
                launch[0] = '\0';
                if (machine->system && machine->system->launch_path[0]) {
                    strncpy(launch, machine->system->launch_path, sizeof(launch) - 1);
                    launch[sizeof(launch) - 1] = '\0';
                }
                bool was_picker = is_picker;
                eject_rom(&machine, &program, win);
                running_rom = false;
                is_picker = false;

                if (launch_path_ok(launch) && switch_to_app(launch, win, ren, &tex, &program, &machine)) {
                    running_rom = true;
                } else if (from_argv) {
                    quit = true;
                } else if (was_picker && !launch[0]) {
                    quit = true;
                } else {
                    if (switch_to_app(PICKER_PATH, win, ren, &tex, &program, &machine)) {
                        running_rom = true;
                        is_picker = true;
                    } else {
                        quit = true;
                    }
                }
            }
        }

        if (running_rom && machine && machine->cpu && !machine->cpu->halted) {
            vm_mid_frame = !machine->system->yielded && !vm_yielded(machine->cpu);
        }

        Uint64 t_after_tick = g_frame_debug ? SDL_GetPerformanceCounter() : 0;

        if (g_menu_debug && machine && machine->cpu && machine->cpu->memory) {
            int32_t hover = read_le32(machine->cpu->memory, machine->cpu->memory_size, MB_HOVER_ADDR);
            if (hover != last_hover) {
                double t_ms = (double)(SDL_GetPerformanceCounter() - t_program_start) * 1000.0 / (double)perf_freq;
                fprintf(stderr, "[menu] t=%.1f MB_HOVER %d -> %d  (host_iter=%llu commits=%llu)\n",
                    t_ms, last_hover, hover,
                    (unsigned long long)host_loop_iters, (unsigned long long)machine->system->frame_commits);
                last_hover = hover;
            }
            uint64_t commits_now = machine->system->frame_commits;
            if (commits_now == last_frame_commits && running_rom) {
                // This host loop iteration presented a frame without the VM ever
                // reaching its end-frame/yield point -- screen_pixels is stale,
                // i.e. the same picture is being re-presented while state moved on.
                double t_ms = (double)(SDL_GetPerformanceCounter() - t_program_start) * 1000.0 / (double)perf_freq;
                fprintf(stderr, "[menu] t=%.1f STALL: host_iter=%llu presented with no new VM frame commit (commits=%llu, cycles=%d)\n",
                    t_ms, (unsigned long long)host_loop_iters, (unsigned long long)commits_now,
                    machine->system->last_tick_cycles);
            }
            last_frame_commits = commits_now;
        }

        // A guest stopped mid-frame has nothing new to show: screen_pixels
        // still holds the last committed frame. Presenting it again and then
        // sleeping out the rest of the 16.7ms budget spends most of a frame
        // period doing nothing while the app is behind, so an app frame that
        // needs N cycle-budgets takes N*16.7ms of wall clock for only
        // N*<budget> of actual work. Loop straight back into machine_tick
        // instead and let it catch up at full speed.
        //
        // Bounded by CATCHUP_BUDGET_MS so a guest stuck in a loop that never
        // yields still gets its window pumped and redrawn rather than
        // appearing hung.
        const double CATCHUP_BUDGET_MS = 100.0;
        if (vm_mid_frame && !g_dialog.active) {
            // Measured from the last present, not from this iteration's start:
            // each catch-up iteration is one cycle budget, so a per-iteration
            // clock would never reach the cap.
            double since_present_ms = (double)(SDL_GetPerformanceCounter() - t_last_present)
                                      * 1000.0 / (double)perf_freq;
            if (since_present_ms < CATCHUP_BUDGET_MS) continue;
        }
        t_last_present = SDL_GetPerformanceCounter();

        uint32_t* pixels;
        int pitch;
        SDL_LockTexture(tex, NULL, (void**)&pixels, &pitch);

        if (machine && machine->system->screen_pixels) {
            if (g_dialog.active) dialog_draw(&g_dialog);
            int w = machine->system->screen_width;
            int h = machine->system->screen_height;
            if (w > g_win_w) w = g_win_w;
            if (h > g_win_h) h = g_win_h;
            int src_stride = machine->system->screen_width;
            int dst_stride = pitch / 4;
            // screen_pixels is [A,R,G,B] per pixel; the ABGR8888 texture wants the
            // exact byte-reversed word, so this is a single bswap per pixel rather
            // than three shifted loads/stores with a per-pixel bounds check.
            for (int y = 0; y < h; y++) {
                const uint32_t* srow = (const uint32_t*)(machine->system->screen_pixels + (size_t)y * src_stride * 4);
                uint32_t* drow = pixels + (size_t)y * dst_stride;
                for (int x = 0; x < w; x++) {
                    drow[x] = __builtin_bswap32(srow[x]);
                }
            }
        }

        SDL_UnlockTexture(tex);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        if (g_frame_debug) {
            Uint64 t_frame_end = SDL_GetPerformanceCounter();
            double poll_ms = (double)(t_after_poll - t_frame_start) * 1000.0 / (double)perf_freq;
            double tick_ms = (double)(t_after_tick - t_after_poll) * 1000.0 / (double)perf_freq;
            double render_ms = (double)(t_frame_end - t_after_tick) * 1000.0 / (double)perf_freq;
            double total_ms = (double)(t_frame_end - t_frame_start) * 1000.0 / (double)perf_freq;
            phase_stat_add(&stat_poll, poll_ms);
            phase_stat_add(&stat_tick, tick_ms);
            phase_stat_add(&stat_render, render_ms);
            phase_stat_add(&stat_total, total_ms);
            frame_debug_count++;

            double window_s = (double)(t_frame_end - frame_debug_window_start) / (double)perf_freq;
            if (window_s >= 1.0) {
                fprintf(stderr,
                    "[frame] n=%d avg/max ms  poll=%.3f/%.3f  tick=%.3f/%.3f  render=%.3f/%.3f  total=%.3f/%.3f\n",
                    frame_debug_count,
                    stat_poll.sum_ms / frame_debug_count, stat_poll.max_ms,
                    stat_tick.sum_ms / frame_debug_count, stat_tick.max_ms,
                    stat_render.sum_ms / frame_debug_count, stat_render.max_ms,
                    stat_total.sum_ms / frame_debug_count, stat_total.max_ms);
                phase_stat_reset(&stat_poll);
                phase_stat_reset(&stat_tick);
                phase_stat_reset(&stat_render);
                phase_stat_reset(&stat_total);
                frame_debug_count = 0;
                frame_debug_window_start = t_frame_end;
            }
        }

        // Manual frame pacing: with vsync off (the default, see above) nothing
        // else caps the loop rate, so sleep off whatever's left of the frame
        // budget rather than spinning the CPU or racing ahead of the display.
        Uint64 t_now = SDL_GetPerformanceCounter();
        double elapsed_ms = (double)(t_now - t_frame_start) * 1000.0 / (double)perf_freq;
        if (elapsed_ms < FRAME_TARGET_MS) {
            SDL_Delay((Uint32)(FRAME_TARGET_MS - elapsed_ms));
        }
    }

    eject_rom(&machine, &program, win);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();
    return 0;
}
