#include "host_fonts.h"
#include "system.h"
#include "chicago.h"
#include "geneva.h"
#include "monaco.h"

const uint8_t* host_font_chicago(void) { return chicago12x12_cff; }
unsigned int host_font_chicago_len(void) { return chicago12x12_cff_len; }
const uint8_t* host_font_geneva(void) { return geneva12_cff; }
unsigned int host_font_geneva_len(void) { return geneva12_cff_len; }
const uint8_t* host_font_monaco(void) { return monaco12_cff; }
unsigned int host_font_monaco_len(void) { return monaco12_cff_len; }

const uint8_t* host_font_data_id(int font_id) {
    switch (font_id) {
        case FONT_GENEVA: return geneva12_cff;
        case FONT_MONACO: return monaco12_cff;
        default:          return chicago12x12_cff; /* Chicago + legacy basic */
    }
}

int host_font_nbytes_id(int font_id) {
    switch (font_id) {
        case FONT_GENEVA: return (int)geneva12_cff_len;
        case FONT_MONACO: return (int)monaco12_cff_len;
        default:          return (int)chicago12x12_cff_len;
    }
}
