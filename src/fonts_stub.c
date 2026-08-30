#include "host_fonts.h"
#include <stddef.h>

const uint8_t* host_font_chicago(void) { return NULL; }
unsigned int host_font_chicago_len(void) { return 0; }
const uint8_t* host_font_geneva(void) { return NULL; }
unsigned int host_font_geneva_len(void) { return 0; }
const uint8_t* host_font_monaco(void) { return NULL; }
unsigned int host_font_monaco_len(void) { return 0; }

const uint8_t* host_font_data_id(int font_id) {
    (void)font_id;
    return NULL;
}

int host_font_nbytes_id(int font_id) {
    (void)font_id;
    return 0;
}
