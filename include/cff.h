#ifndef CFF_H
#define CFF_H

/* Cloister Font Format / Uxn UFX sizes:
 *   2304  (0x0900)  8x8,  1 tile/glyph
 *   8448  (0x2100) 16x16, 4 tiles/glyph
 *   18688 (0x4900) 24x24, 9 tiles/glyph
 */

#define CFF_LEN_UF1 2304
#define CFF_LEN_UF2 8448
#define CFF_LEN_UF3 18688

static inline int cff_tile_size(int nbytes) {
    if (nbytes == CFF_LEN_UF1) return 8;
    if (nbytes == CFF_LEN_UF2) return 16;
    if (nbytes == CFF_LEN_UF3) return 24;
    return 0;
}

static inline int cff_glyph_bytes(int tile) {
    int n = tile / 8;
    if (n < 1) return 0;
    return n * n * 8;
}

static inline int cff_file_len(int tile) {
    return 256 + 256 * cff_glyph_bytes(tile);
}

#endif
