#ifndef FLUXIO_BUILD_H
#define FLUXIO_BUILD_H

/* Compiling a Fluxio source file all the way to a runnable image, in memory:
 * the whole `fluxioc` pipeline (lex + includes, parse, Kelvin check, codegen)
 * and, when the app needs it, the `fluxlink` merge on top -- with no
 * intermediate files. This is what lets cloister launch an `.fx` app straight
 * from source the way it already does for `.lux`.
 *
 * Kept out of cloister.c so it can be linked (and tested) without SDL: the
 * ABI conformance suite checks that what this produces is byte-identical to
 * what the Makefile's fluxioc+fluxlink pipeline writes out. */

#include <stdint.h>
#include <stddef.h>

/* Builds `path` into a raw image loaded at base_addr, *out_len bytes long;
 * caller must free() it. Returns NULL on any lex/parse/version/codegen/link
 * error, having printed a "<tool>: ..." diagnostic to stderr. */
uint8_t* fx_build_image(const char* path, int32_t base_addr, const char* tool, size_t* out_len);

#endif /* FLUXIO_BUILD_H */
