#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <math.h>

#include "machine.h"
#include "vfs.h"
#include "compiler.h"
#include "dialog.h"
#include "chicago.h"

#define WIN_WIDTH 960
#define WIN_HEIGHT 720
#define MAX_ROMS 100
#define LIST_X 40
#define LIST_Y 72
#define LIST_ROW 20

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
    int next = (m->system->event_tail + 1) % 64;
    if (next != m->system->event_head) {
        m->system->events[m->system->event_tail] = (type << 24) | (data & 0xFFFFFF);
        m->system->event_tail = next;
    }
    if (type == 0) {
        system_push_kbd_event(m->system, (uint8_t)type, (int32_t)(data & 0xFFFFFF), mods);
    } else if (type >= 2 && type <= 4) {
        int32_t mx = (int32_t)(data >> 12);
        int32_t my = (int32_t)(data & 0xFFF);
        uint8_t btn = (type == 2) ? 0 : (uint8_t)(mods & 0xFF);
        system_push_mouse_event(m->system, (uint8_t)type, mx, my, btn);
    }
}

static uint32_t current_modifiers(SDL_Keymod m) {
    uint32_t mods = 0;
    if (m & KMOD_SHIFT) mods |= 1;
    if (m & KMOD_CTRL)  mods |= 2;
    if (m & KMOD_ALT)   mods |= 4;
    if (m & KMOD_GUI)   mods |= 8;
    return mods;
}

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
                     SDL_Window* win) {
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
        program = (uint8_t*)file_content;
        code_len = (size_t)fsize;
    }

    Machine* machine = machine_create(program, (uint32_t)code_len, GRAPHICAL_BASE_ADDRESS,
                                      32 * 1024 * 1024, false);
    if (!machine) {
        free(program);
        fprintf(stderr, "Failed to create machine for %s\n", path);
        return false;
    }

    if (machine->system) {
        system_set_resolution(machine->system, WIN_WIDTH, WIN_HEIGHT);
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

static void fill_rect(uint32_t* pixels, int pitch, int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= WIN_HEIGHT) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= WIN_WIDTH) continue;
            pixels[j * (pitch / 4) + i] = color;
        }
    }
}

static void draw_char(uint32_t* pixels, int pitch, int x, int y, char c, uint32_t color) {
    unsigned char* data = chicago12x12_cff;
    int width = data[(uint8_t)c];
    if (width == 0 && c != ' ') return;

    int tile_size = 16;
    int num_v_tiles = tile_size / 8;
    int num_h_tiles = tile_size / 8;
    int tile_count = num_h_tiles * num_v_tiles;
    int offset = 256 + (uint8_t)c * tile_count * 8;
    int idx = 0;
    for (int tx = 0; tx < num_h_tiles; tx++) {
        for (int ty = 0; ty < num_v_tiles; ty++) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = data[offset + idx++];
                if (bits == 0) continue;
                int pixel_y = y + ty * 8 + row;
                if (pixel_y < 0 || pixel_y >= WIN_HEIGHT) continue;
                for (int col = 0; col < 8; col++) {
                    if ((bits & (0x80 >> col)) == 0) continue;
                    int pixel_x = x + tx * 8 + col;
                    if (pixel_x < 0 || pixel_x >= WIN_WIDTH) continue;
                    pixels[pixel_y * (pitch / 4) + pixel_x] = color;
                }
            }
        }
    }
    (void)width;
}

static void draw_text(uint32_t* pixels, int pitch, int x, int y, const char* str, uint32_t color) {
    int cur_x = x;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        draw_char(pixels, pitch, cur_x, y, (char)c, color);
        int w = chicago12x12_cff[c];
        cur_x += (w > 0 ? w : 8) + 1;
    }
}

static bool ends_with(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n >= e && strcmp(name + n - e, ext) == 0;
}

static void stem_copy(const char* name, char* out, size_t n) {
    strncpy(out, name, n - 1);
    out[n - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

typedef struct {
    char path[256];
    char label[256];
} RomEntry;

static int cmp_rom(const void* a, const void* b) {
    return strcmp(((const RomEntry*)a)->label, ((const RomEntry*)b)->label);
}

static int scan_roms(RomEntry* roms, int cap) {
    char bins[MAX_ROMS][256];
    int nbin = 0;
    DIR* d = opendir("apps");
    if (!d) return 0;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL && nbin < MAX_ROMS) {
        if (ends_with(ent->d_name, ".bin") && strcmp(ent->d_name, "Shell.bin") != 0) {
            strncpy(bins[nbin], ent->d_name, 255);
            bins[nbin][255] = '\0';
            nbin++;
        }
    }
    rewinddir(d);

    int n = 0;
    for (int i = 0; i < nbin && n < cap; i++) {
        snprintf(roms[n].path, sizeof(roms[n].path), "apps/%s", bins[i]);
        stem_copy(bins[i], roms[n].label, sizeof(roms[n].label));
        n++;
    }

    while ((ent = readdir(d)) != NULL && n < cap) {
        if (!ends_with(ent->d_name, ".lux")) continue;
        if (strcmp(ent->d_name, "Shell.lux") == 0) continue;
        char stem[256];
        stem_copy(ent->d_name, stem, sizeof(stem));
        int have_bin = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(roms[i].label, stem) == 0) { have_bin = 1; break; }
        }
        if (have_bin) continue;
        snprintf(roms[n].path, sizeof(roms[n].path), "apps/%s", ent->d_name);
        strncpy(roms[n].label, stem, sizeof(roms[n].label) - 1);
        roms[n].label[sizeof(roms[n].label) - 1] = '\0';
        n++;
    }
    closedir(d);

    qsort(roms, (size_t)n, sizeof(RomEntry), cmp_rom);
    return n;
}

static int list_hit(int mx, int my, int nroms) {
    if (mx < LIST_X || mx > WIN_WIDTH - 40) return -1;
    if (my < LIST_Y || my >= LIST_Y + nroms * LIST_ROW) return -1;
    return (my - LIST_Y) / LIST_ROW;
}

static void paint_picker(uint32_t* pixels, int pitch, const RomEntry* roms, int nroms, int selected) {
    const uint32_t white = 0xFFFFFFFF;
    const uint32_t black = 0xFF000000;
    const uint32_t desk  = 0xFFCCCCCC;
    fill_rect(pixels, pitch, 0, 0, WIN_WIDTH, WIN_HEIGHT, desk);
    fill_rect(pixels, pitch, 0, 0, WIN_WIDTH, 36, white);
    fill_rect(pixels, pitch, 0, 35, WIN_WIDTH, 1, black);
    draw_text(pixels, pitch, 16, 10, "Cloister", black);
    draw_text(pixels, pitch, 16, 48, "Select a program    Enter or click to run    Esc to quit", black);

    if (nroms == 0) {
        draw_text(pixels, pitch, LIST_X, LIST_Y, "No programs in apps/", black);
        return;
    }
    for (int i = 0; i < nroms; i++) {
        int y = LIST_Y + i * LIST_ROW;
        if (i == selected) {
            fill_rect(pixels, pitch, LIST_X - 8, y - 2, WIN_WIDTH - 2 * LIST_X, LIST_ROW, black);
            draw_text(pixels, pitch, LIST_X, y, roms[i].label, white);
        } else {
            draw_text(pixels, pitch, LIST_X, y, roms[i].label, black);
        }
    }
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("Cloister", 100, 100, WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, WIN_WIDTH, WIN_HEIGHT);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = audio_callback;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 0);

    RomEntry roms[MAX_ROMS];
    int nroms = scan_roms(roms, MAX_ROMS);
    int selected = 0;

    Machine* machine = NULL;
    uint8_t* program = NULL;
    bool from_argv = argc > 1;
    bool running_rom = false;

    if (from_argv) {
        if (!load_app(argv[1], &program, &machine, win)) return 1;
        running_rom = true;
    }

    bool quit = false;
    while (!quit) {
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
            } else if (!running_rom) {
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        quit = true;
                    } else if (e.key.keysym.sym == SDLK_UP && nroms > 0) {
                        selected = (selected + nroms - 1) % nroms;
                    } else if (e.key.keysym.sym == SDLK_DOWN && nroms > 0) {
                        selected = (selected + 1) % nroms;
                    } else if (e.key.keysym.sym == SDLK_RETURN && nroms > 0) {
                        if (load_app(roms[selected].path, &program, &machine, win)) {
                            running_rom = true;
                        }
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                    int hit = list_hit(e.button.x, e.button.y, nroms);
                    if (hit >= 0) {
                        selected = hit;
                        if (load_app(roms[selected].path, &program, &machine, win)) {
                            running_rom = true;
                        }
                    }
                }
            } else if (machine) {
                if (e.type == SDL_KEYDOWN) {
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        uint32_t mods = current_modifiers((SDL_Keymod)e.key.keysym.mod);
                        queue_event(machine, 0, (uint32_t)code & 0xFFFFFF, mods);
                    }
                } else if (e.type == SDL_KEYUP) {
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        queue_event(machine, 1, (uint32_t)code & 0xFFFFFF, 0);
                    }
                } else if (e.type == SDL_MOUSEMOTION) {
                    machine->system->mouse_x = e.motion.x;
                    machine->system->mouse_y = e.motion.y;
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

        if (running_rom && machine) {
            if (!machine_tick(machine)) {
                if (from_argv) {
                    quit = true;
                } else {
                    eject_rom(&machine, &program, win);
                    running_rom = false;
                }
            }
        }

        uint32_t* pixels;
        int pitch;
        SDL_LockTexture(tex, NULL, (void**)&pixels, &pitch);

        if (!running_rom) {
            paint_picker(pixels, pitch, roms, nroms, selected);
        } else if (machine && machine->system->screen_pixels) {
            if (g_dialog.active) dialog_draw(&g_dialog);
            int w = machine->system->screen_width;
            int h = machine->system->screen_height;
            for (int y = 0; y < h; y++) {
                if (y >= WIN_HEIGHT) break;
                for (int x = 0; x < w; x++) {
                    if (x >= WIN_WIDTH) break;
                    int src_idx = (y * w + x) * 4;
                    uint8_t r = machine->system->screen_pixels[src_idx + 1];
                    uint8_t g = machine->system->screen_pixels[src_idx + 2];
                    uint8_t b = machine->system->screen_pixels[src_idx + 3];
                    pixels[y * (pitch / 4) + x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }

        SDL_UnlockTexture(tex);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    eject_rom(&machine, &program, win);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();
    return 0;
}
