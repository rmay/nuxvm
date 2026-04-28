# CFF

## Cloister Font Format


CFF is a proportional font format.
The CFF file begins with 256 bytes corresponding to the width in pixels of each of the 256 glyphs in the spritesheet, followed by the pixel data in the .icn format for each character.


The ICN defines the icon format, a series of bits equivalent to pixels in a 8x8 tile. The data for each tile is made up of 64 bits, or 8 bytes, in which each bit is a pixel. Since the ICN file is nothing but a series of 8x8 bitmaps without a header to indicate how large the image should be drawn, the size will be specified in the filename in in two decimal byte numbers, separated by a x. If a file name is lacking that information, the default is 24x24.



**Bit Grid:**

| Row | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | Hex  |
|-----|---|---|---|---|---|---|---|---|------|
| 0   | 0 | 0 | **1** | **1**  | **1**  | **1**  | 0 | 0 | `3C` |
| 1   | 0 | **1**  | 0 | 0 | 0 | 0 | **1**  | 0 | `42` |
| 2   | **1**  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | `80` |
| 3   | **1**  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | `80` |
| 4   | **1**  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | `80` |
| 5   | **1**  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | `80` |
| 6   | 0 | **1**  | 0 | 0 | 0 | 0 | **1**  | 0 | `42` |
| 7   | 0 | 0 | **1**  | **1**  | **1**  | **1**  | 0 | 0 | `3C` |



**Raw Hex:**
3C 42 80 80 80 80 42 3C

