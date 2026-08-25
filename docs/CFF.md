# CFF

## Cloister Font Format

CFF is a proportional 1-bit font format, the same layout as Uxn UFX (`.uf1` / `.uf2` / `.uf3`).

The file begins with 256 bytes: the advance width in pixels of each of the 256 glyphs. Pixel data follows in the ICN format.

ICN is a series of 8×8 tiles. Each tile is 64 bits (8 bytes), MSB = leftmost pixel of the row. Tiles inside a glyph are stored **column-major** (vertical strip, then the next column) so a narrow glyph can skip unused columns.

Glyph cell size is taken from **file length**, not from the filename:

| Length | Hex | Cell | Tiles / glyph |
| ------ | --- | ---- | ------------- |
| 2304 | `0x0900` | 8×8 | 1 |
| 8448 | `0x2100` | 16×16 | 4 |
| 18688 | `0x4900` | 24×24 | 9 |

`resources/chicago12x12.cff` is 8448 bytes (16×16 storage). The `12x12` in the name is the visual cap height, not the tile grid.

Edit CFF files with **Illumos** (`apps/Illumos.lux`): `./bin/cloister apps/Illumos.bin`.



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

