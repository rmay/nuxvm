#ifndef FLUXIO_INCLUDE_H
#define FLUXIO_INCLUDE_H

#include "fluxio_token.h"

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

#endif /* FLUXIO_INCLUDE_H */
