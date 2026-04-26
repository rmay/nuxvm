# CFF

## Cloister Font Format

The CFF defines the icon format, a series of bits equivalent to pixels in a 8x8 tile. The data for each tile is made up of 64 bits, or 8 bytes, in which each bit is a pixel. Since the ICN file is nothing but a series of 8x8 bitmaps without a header to indicate how large the image should be drawn, the size will be specified in the filename in in two decimal byte numbers, separated by a x. If a file name is lacking that information, the default is 24x24.
