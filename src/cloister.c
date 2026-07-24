#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <math.h>

#include "machine.h"
#include "chicago.h"
#include "vfs.h"
#include "compiler.h"
#include "dialog.h"

#define WIN_WIDTH 960
#define WIN_HEIGHT 720

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
        
        if (t < fade) {
            env = t / fade;
        } else if (t > duration - fade) {
            env = (duration - t) / fade;
        }
        
        int16_t val = (int16_t)(env * 0.25 * 32767.0 * sin(2.0 * M_PI * freq * t));
        buffer[i] = val;
        audio_samples_played++;
    }
}

// Drawing chicago font directly to a 32-bit pixel buffer
static void draw_char(uint32_t* pixels, int pitch, int x, int y, char c, uint32_t color, int scale) {
    unsigned char* data = pkg_system_chicago12x12_cff;
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
                
                int row_abs = ty * 8 + row;
                int start_y = row_abs * scale;
                int end_y = (row_abs + 1) * scale;
                
                for (int py = start_y; py < end_y; py++) {
                    int pixel_y = y + py;
                    if (pixel_y < 0 || pixel_y >= WIN_HEIGHT) continue;
                    
                    for (int col = 0; col < 8; col++) {
                        if ((bits & (0x80 >> col)) == 0) continue;
                        
                        int col_abs = tx * 8 + col;
                        int start_x = col_abs * scale;
                        int end_x = (col_abs + 1) * scale;
                        
                        for (int px = start_x; px < end_x; px++) {
                            int pixel_x = x + px;
                            if (pixel_x < 0 || pixel_x >= WIN_WIDTH) continue;
                            
                            pixels[pixel_y * (pitch / 4) + pixel_x] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_text(uint32_t* pixels, int pitch, int x, int y, const char* str, uint32_t color, int scale) {
    int cur_x = x;
    while (*str) {
        char c = *str++;
        draw_char(pixels, pitch, cur_x, y, c, color, scale);
        // approx width
        cur_x += 10 * scale; 
    }
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


static void queue_event(Machine* m, uint32_t type, uint32_t data, uint32_t mods) {
    if (!m || !m->system) return;
    int next = (m->system->event_tail + 1) % 64;
    if (next != m->system->event_head) {
        m->system->events[m->system->event_tail] = (type << 24) | (data & 0xFFFFFF);
        m->system->event_tail = next;
    }
    if (type == 0) {
        // Only KeyDown feeds /sys/kbd — the Go host never queued KeyUps there.
        system_push_kbd_event(m->system, (uint8_t)type, (int32_t)(data & 0xFFFFFF), mods);
    } else if (type >= 2 && type <= 4) {
        int32_t mx = (int32_t)(data >> 12);
        int32_t my = (int32_t)(data & 0xFFF);
        uint8_t btn = (type == 3) ? 1 : 0;
        system_push_mouse_event(m->system, (uint8_t)type, mx, my, btn);
    }
}

// Packs SDL modifier state into the bitmask expected by Lux apps
// (mirrors lib/event.lux MOD_*: shift=1, ctrl=2, alt=4, cmd=8).
static uint32_t current_modifiers(SDL_Keymod m) {
    uint32_t mods = 0;
    if (m & KMOD_SHIFT) mods |= 1;
    if (m & KMOD_CTRL)  mods |= 2;
    if (m & KMOD_ALT)   mods |= 4;
    if (m & KMOD_GUI)   mods |= 8;
    return mods;
}

// Maps an SDL keycode to the integer keycode that Lux apps see. Letters/digits
// become ASCII; arrows use the dedicated 17-20 codes that Snake.lux and other
// apps key off. Returns false for keys that should not produce an event.
static bool translate_key(SDL_Keycode k, bool shift, int32_t* out) {
    switch (k) {
        case SDLK_UP:        *out = 17; return true;
        case SDLK_DOWN:      *out = 18; return true;
        case SDLK_LEFT:      *out = 19; return true;
        case SDLK_RIGHT:     *out = 20; return true;
        case SDLK_PAGEUP:    *out = 21; return true;
        case SDLK_PAGEDOWN:  *out = 22; return true;
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

// The single modal file dialog; opened via the /sys/dialog VFS write (or
// SCI_OPEN_FILE_DIALOG), driven by the SDL event loop below.
static FileDialog g_dialog;

static void cloister_open_dialog(void* ctx) {
    FileDialog* d = (FileDialog*)ctx;
    dialog_open(d, d->sys);
}

// Load a .bin (raw bytecode) or .lux (compile in-process) at the graphical base
// address. On success, frees any previous *program_out / *machine_out and replaces
// them. Returns true if a machine was created.
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

static bool is_launchable(const char* name) {
    size_t n = strlen(name);
    if (n < 5) return false;
    if (strcmp(name + n - 4, ".bin") == 0) return true;
    if (strcmp(name + n - 4, ".lux") == 0) return true;
    return false;
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
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 0);
    }

    char apps[100][256];
    int num_apps = 0;
    
    DIR* d = opendir("apps");
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL && num_apps < 100) {
            if (is_launchable(dir->d_name)) {
                strncpy(apps[num_apps], dir->d_name, 255);
                apps[num_apps][255] = '\0';
                num_apps++;
            }
        }
        closedir(d);
    }

    bool launcher_mode = true;
    int selected_index = 0;
    bool esc_menu_open = false;
    int esc_menu_hover = 0; // 0=none, 1=continue, 2=restart, 3=quit

    Machine* machine = NULL;
    uint8_t* program = NULL;

    if (argc > 1) {
        if (load_app(argv[1], &program, &machine, win)) {
            launcher_mode = false;
        } else {
            return 1;
        }
    }

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (!launcher_mode && g_dialog.active) {
                // Modal file dialog consumes all input while active.
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
                // Mouse motion / key up are consumed silently.
            } else if (e.type == SDL_KEYDOWN) {
                if (launcher_mode) {
                    if (esc_menu_open) {
                        if (e.key.keysym.sym == SDLK_UP) {
                            esc_menu_hover--;
                            if (esc_menu_hover < 1) esc_menu_hover = 3;
                        } else if (e.key.keysym.sym == SDLK_DOWN) {
                            esc_menu_hover++;
                            if (esc_menu_hover > 3) esc_menu_hover = 1;
                        } else if (e.key.keysym.sym == SDLK_RETURN) {
                            if (esc_menu_hover == 1 || esc_menu_hover == 2) {
                                esc_menu_open = false;
                            } else if (esc_menu_hover == 3) {
                                quit = true;
                            }
                        } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                            esc_menu_open = false;
                        }
                    } else {
                        if (e.key.keysym.sym == SDLK_ESCAPE) {
                            esc_menu_open = true;
                            esc_menu_hover = 1;
                        } else if (e.key.keysym.sym == SDLK_UP) {
                            selected_index--;
                            if (selected_index < 0) selected_index = num_apps - 1;
                        } else if (e.key.keysym.sym == SDLK_DOWN) {
                            selected_index++;
                            if (selected_index >= num_apps) selected_index = 0;
                        } else if (e.key.keysym.sym == SDLK_RETURN) {
                            if (num_apps > 0) {
                                char path[512];
                                snprintf(path, sizeof(path), "apps/%s", apps[selected_index]);
                                if (load_app(path, &program, &machine, win)) {
                                    launcher_mode = false;
                                }
                            }
                        }
                    }
                } else if (machine) {
                    // App mode: translate the key and deliver it to the app.
                    // Escape is delivered too (27) — apps handle their own menus;
                    // we return to the launcher when the app halts.
                    int32_t code;
                    bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (translate_key(e.key.keysym.sym, shift, &code)) {
                        uint32_t mods = current_modifiers((SDL_Keymod)e.key.keysym.mod);
                        queue_event(machine, 0, (uint32_t)code & 0xFFFFFF, mods);
                        if (machine->system->get_vector) {
                            uint32_t vec = machine->system->get_vector(machine->system, 4); // Controller
                            if (vec != 0) vm_call_vector(machine->cpu, vec);
                        }
                    }
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                if (launcher_mode && esc_menu_open) {
                    int mx = e.motion.x, my = e.motion.y;
                    int menu_x = (WIN_WIDTH - 200) / 2;
                    int menu_y = (WIN_HEIGHT - 160) / 2;
                    int rel_x = mx - menu_x;
                    int rel_y = my - menu_y;
                    int hover = 0;
                    if (rel_x >= 20 && rel_x <= 180) {
                        if (rel_y >= 50 && rel_y <= 80) hover = 1;
                        else if (rel_y >= 85 && rel_y <= 115) hover = 2;
                        else if (rel_y >= 120 && rel_y <= 150) hover = 3;
                    }
                    if (hover != 0) esc_menu_hover = hover;
                } else if (!launcher_mode && machine) {
                    machine->system->mouse_x = e.motion.x;
                    machine->system->mouse_y = e.motion.y;
                    queue_event(machine, 2, (e.motion.x << 12) | (e.motion.y & 0xFFF), 0);
                    if (machine->system->get_vector) {
                        uint32_t vec = machine->system->get_vector(machine->system, 5); // Mouse Vector

                        if (vec != 0) {
                            vm_call_vector(machine->cpu, vec);
                        }

                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (launcher_mode && esc_menu_open) {
                    if (esc_menu_hover == 1 || esc_menu_hover == 2) esc_menu_open = false;
                    else if (esc_menu_hover == 3) quit = true;
                    else esc_menu_open = false;
                } else if (!launcher_mode && machine) {
                    machine->system->mouse_btn |= 1;
                    queue_event(machine, 3, (e.button.x << 12) | (e.button.y & 0xFFF), 0);
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (!launcher_mode && machine) {
                    machine->system->mouse_btn &= ~1;
                    queue_event(machine, 4, (e.button.x << 12) | (e.button.y & 0xFFF), 0);
                }
            } else if (e.type == SDL_KEYUP && !launcher_mode && machine) {
                int32_t code;
                bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                if (translate_key(e.key.keysym.sym, shift, &code)) {
                    queue_event(machine, 1, (uint32_t)code & 0xFFFFFF, 0); // type 1 = KeyUp
                    if (machine->system->get_vector) {
                        uint32_t vec = machine->system->get_vector(machine->system, 4); // Controller
                        if (vec != 0) vm_call_vector(machine->cpu, vec);
                    }
                }
            }
        }

        if (!launcher_mode && machine) {
            // Tick machine
            if (!machine_tick(machine)) {
                launcher_mode = true;
                SDL_SetWindowTitle(win, "Cloister");
                dialog_free(&g_dialog);
                g_dialog.sys = NULL;
            }
        }

        // Audio is now triggered synchronously via SCI_PLAY_SOUND, audio control port,
        // or /sys/audio VFS writes (see cloister_play_sound + vfs audio handler).

        uint32_t* pixels;
        int pitch;
        SDL_LockTexture(tex, NULL, (void**)&pixels, &pitch);

        if (launcher_mode) {
            // Draw launcher
            fill_rect(pixels, pitch, 0, 0, WIN_WIDTH, WIN_HEIGHT, 0xFF141414); // Dark gray
            draw_text(pixels, pitch, 20, 20, "--- NUXVM LAUNCHER ---", 0xFFFFFFFF, 1);
            
            for (int i = 0; i < num_apps; i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s%s", (i == selected_index) ? "> " : "  ", apps[i]);
                draw_text(pixels, pitch, 20, 50 + i * 20, buf, 0xFFFFFFFF, 1);
            }

            if (esc_menu_open) {
                int menu_x = (WIN_WIDTH - 200) / 2;
                int menu_y = (WIN_HEIGHT - 160) / 2;
                fill_rect(pixels, pitch, menu_x - 2, menu_y - 2, 204, 164, 0xFF000000);
                fill_rect(pixels, pitch, menu_x, menu_y, 200, 160, 0xFFFFFFFF);
                
                draw_text(pixels, pitch, menu_x + 35, menu_y + 15, "System Menu", 0xFF000000, 1);
                
                // Buttons
                for (int i = 1; i <= 3; i++) {
                    fill_rect(pixels, pitch, menu_x + 20, menu_y + 15 + i * 35, 160, 30, 0xFF000000);
                    uint32_t btn_color = (esc_menu_hover == i) ? 0xFFAAAAAA : 0xFFFFFFFF;
                    fill_rect(pixels, pitch, menu_x + 21, menu_y + 16 + i * 35, 158, 28, btn_color);
                    
                    const char* text = (i == 1) ? "Continue" : (i == 2) ? "Restart App" : "Quit";
                    draw_text(pixels, pitch, menu_x + 50, menu_y + 21 + i * 35, text, 0xFF000000, 1);
                }
            }
        } else if (machine && machine->system->screen_pixels) {
            if (g_dialog.active) {
                dialog_draw(&g_dialog);
            }
            int w = machine->system->screen_width;
            int h = machine->system->screen_height;
            for (int y = 0; y < h; y++) {
                if (y >= WIN_HEIGHT) break;
                for (int x = 0; x < w; x++) {
                    if (x >= WIN_WIDTH) break;
                    int src_idx = (y * w + x) * 4;
                    uint8_t a = 0xFF;
                    uint8_t r = machine->system->screen_pixels[src_idx+1];
                    uint8_t g = machine->system->screen_pixels[src_idx+2];
                    uint8_t b = machine->system->screen_pixels[src_idx+3];
                    pixels[y * (pitch / 4) + x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }

        SDL_UnlockTexture(tex);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        // No fixed delay; rely on SDL_RENDERER_PRESENTVSYNC + natural loop timing for better input responsiveness
    }

    if (machine) machine_free(machine);
    if (program) free(program);
    
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_CloseAudioDevice(audio_device);
    SDL_Quit();

    return 0;
}
