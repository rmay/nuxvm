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


static void queue_event(Machine* m, uint32_t type, uint32_t data) {
    if (!m || !m->system) return;
    int next = (m->system->event_tail + 1) % 64;
    if (next != m->system->event_head) {
        m->system->events[m->system->event_tail] = (type << 24) | (data & 0xFFFFFF);
        m->system->event_tail = next;
    }
    if (type == 0 || type == 1) {
        system_push_kbd_event(m->system, (uint8_t)type, (int32_t)(data & 0xFFFFFF), 0);
    } else if (type >= 2 && type <= 4) {
        int32_t mx = (int32_t)(data >> 12);
        int32_t my = (int32_t)(data & 0xFFF);
        uint8_t btn = (type == 3) ? 1 : 0;
        system_push_mouse_event(m->system, (uint8_t)type, mx, my, btn);
    }
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

    // Register our simple synth with the VFS layer for /sys/audio writes from Lux code
    vfs_set_sound_handler(cloister_play_sound);

    char apps[100][256];
    int num_apps = 0;
    
    DIR* d = opendir("apps");
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, ".bin")) {
                strncpy(apps[num_apps++], dir->d_name, 255);
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
        FILE* f = fopen(argv[1], "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* file_content = malloc(fsize + 1);
            fread(file_content, 1, fsize, f);
            file_content[fsize] = '\0';
            fclose(f);
            
            if (strstr(argv[1], ".lux")) {
                size_t code_len = 0;
                program = compile_source(file_content, GRAPHICAL_BASE_ADDRESS, &code_len, false);
                free(file_content);
                if (program) {
                    machine = machine_create(program, code_len, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
                    if (machine && machine->system) system_set_resolution(machine->system, WIN_WIDTH, WIN_HEIGHT);
                } else {
                    fprintf(stderr, "Failed to compile %s\n", argv[1]);
                    return 1;
                }
            } else {
                program = (uint8_t*)file_content;
                machine = machine_create(program, fsize, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
                if (machine && machine->system) system_set_resolution(machine->system, WIN_WIDTH, WIN_HEIGHT);
            }
            if (machine && machine->system) machine->system->play_sound = cloister_play_sound;
            launcher_mode = false;
        } else {
            fprintf(stderr, "Could not open %s\n", argv[1]);
        }
    }

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
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
                                
                                FILE* f = fopen(path, "rb");
                                if (f) {
                                    fseek(f, 0, SEEK_END);
                                    long fsize = ftell(f);
                                    fseek(f, 0, SEEK_SET);
                                    
                                    if (program) free(program);
                                    program = malloc(fsize);
                                    fread(program, 1, fsize, f);
                                    fclose(f);
                                    
                                    if (machine) machine_free(machine);
                                    machine = machine_create(program, fsize, GRAPHICAL_BASE_ADDRESS, 32 * 1024 * 1024, false);
                                    if (machine && machine->system) {
                                        system_set_resolution(machine->system, WIN_WIDTH, WIN_HEIGHT);
                                        machine->system->play_sound = cloister_play_sound;
                                    }
                                    launcher_mode = false;
                                    
                                    launcher_mode = false;
                                }
                            }
                        }
                    }
                } else {
                    // Send keys to VM (simplified)
                    // we'll leave input translation for later refinement or use standard ASCII
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        launcher_mode = true;
                        esc_menu_open = true;
                        esc_menu_hover = 1;
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
                    queue_event(machine, 2, (e.motion.x << 12) | (e.motion.y & 0xFFF));
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
                    queue_event(machine, 3, (e.button.x << 12) | (e.button.y & 0xFFF));
                }
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                if (!launcher_mode && machine) {
                    machine->system->mouse_btn &= ~1;
                    queue_event(machine, 4, (e.button.x << 12) | (e.button.y & 0xFFF));
                }
            } else if (e.type == SDL_KEYDOWN && !launcher_mode) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    launcher_mode = true;
                    esc_menu_open = true;
                    esc_menu_hover = 1;
                } else {
                    queue_event(machine, 0, e.key.keysym.sym & 0xFFFFFF); // type 0 = KeyDown
                    if (machine->system->get_vector) {
                        uint32_t vec = machine->system->get_vector(machine->system, 4); // Controller
                        if (vec != 0) vm_call_vector(machine->cpu, vec);
                    }
                }
            } else if (e.type == SDL_KEYUP && !launcher_mode) {
                queue_event(machine, 1, e.key.keysym.sym & 0xFFFFFF); // type 1 = KeyUp
                if (machine->system->get_vector) {
                    uint32_t vec = machine->system->get_vector(machine->system, 4); // Controller
                    if (vec != 0) vm_call_vector(machine->cpu, vec);
                }
            } else if (e.type == SDL_KEYUP && !launcher_mode) {
                queue_event(machine, 1, e.key.keysym.sym & 0xFFFFFF); // type 1 = KeyUp
            }
        }

        if (!launcher_mode && machine) {
            // Tick machine
            if (!machine_tick(machine)) {
                launcher_mode = true;
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
