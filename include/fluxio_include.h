#ifndef FLUXIO_INCLUDE_H
#define FLUXIO_INCLUDE_H

#include "fluxio_token.h"
#include <stddef.h>

/* Reads main_path, tokenizes it, and recursively resolves every top-level
 * `include "relative/path.fx";` directive found anywhere in the token
 * stream (paths are resolved relative to the *including* file's own
 * directory; absolute paths are used as-is). Directives are spliced away
 * entirely -- the returned token list contains none.
 *
 * A file is included at most once per compile (subsequent includes of the
 * same resolved path are silently dropped, like a C header guard) and a
 * circular include chain is a compile error. Returns NULL and prints a
 * diagnostic to stderr on any I/O, lex, or include-graph error. */
FxTokenList* fx_load_with_includes(const char* main_path);

/* As above, but also hands back every source file the compile actually read,
 * in resolution order with main_path first. fluxioc turns that list into the
 * ROM's source_sha256 field. *out_files is a malloc'd array of malloc'd
 * strings; free both. Pass NULL for either out-param to ignore it. */
FxTokenList* fx_load_with_includes_tracked(const char* main_path,
                                           char*** out_files, size_t* out_count);

#endif /* FLUXIO_INCLUDE_H */
