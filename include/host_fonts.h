#ifndef HOST_FONTS_H
#define HOST_FONTS_H

#include <stdint.h>

/* Built-in CFF faces. Real blobs live in src/fonts.c (linked into Cloister
 * and drawing tests). Headless tools link src/fonts_stub.c instead. */

const uint8_t* host_font_chicago(void);
unsigned int host_font_chicago_len(void);
const uint8_t* host_font_geneva(void);
unsigned int host_font_geneva_len(void);
const uint8_t* host_font_monaco(void);
unsigned int host_font_monaco_len(void);

/* font_id is FONT_CHICAGO / FONT_BASIC / FONT_GENEVA / FONT_MONACO. */
const uint8_t* host_font_data_id(int font_id);
int host_font_nbytes_id(int font_id);

#endif /* HOST_FONTS_H */
