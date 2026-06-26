#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#define FRAME_WIDTH  64
#define FRAME_HEIGHT 32
#define VIDEO_FRAMEBUFFER_START 0x4000

// RenderFramebuffer returns the video framebuffer as a terminal-safe grid string.
// Caller is responsible for freeing the returned string.
char* display_render_framebuffer(const uint8_t* memory, uint32_t memory_size);

#endif // DISPLAY_H
