#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* display_render_framebuffer(const uint8_t* memory, uint32_t memory_size) {
    const char* lit = "\033[43m  \033[0m"; // yellow background, 2 spaces
    const char* dark = "  ";
    
    // Allocate buffer for the result string
    // Rule: + (--)x64 + \n -> 1 + 128 + 1 + 1 = 131 bytes
    // Row: | (pixels)x64 | \n -> 1 + (64 * ~13) + 1 + 1 -> approx 1000 bytes per row
    // Total approx 33000 bytes
    size_t buffer_size = 40000;
    char* sb = (char*)malloc(buffer_size);
    if (!sb) return NULL;
    
    sb[0] = '\0';
    
    // Top rule
    strcat(sb, "+");
    for (int i = 0; i < FRAME_WIDTH; i++) {
        strcat(sb, "--");
    }
    strcat(sb, "+\n");
    
    for (int row = 0; row < FRAME_HEIGHT; row++) {
        strcat(sb, "|");
        for (int col = 0; col < FRAME_WIDTH; col++) {
            uint32_t offset = VIDEO_FRAMEBUFFER_START + (row * FRAME_WIDTH + col) * 4;
            if (offset + 4 <= memory_size) {
                // Read 4 bytes big endian
                uint32_t pixel = 0;
                pixel |= (uint32_t)memory[offset] << 24;
                pixel |= (uint32_t)memory[offset+1] << 16;
                pixel |= (uint32_t)memory[offset+2] << 8;
                pixel |= (uint32_t)memory[offset+3];
                
                if (pixel != 0) {
                    strcat(sb, lit);
                } else {
                    strcat(sb, dark);
                }
            } else {
                strcat(sb, dark);
            }
        }
        strcat(sb, "|\n");
    }
    
    // Bottom rule
    strcat(sb, "+");
    for (int i = 0; i < FRAME_WIDTH; i++) {
        strcat(sb, "--");
    }
    strcat(sb, "+\n");
    
    return sb;
}
