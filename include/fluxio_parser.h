#ifndef FLUXIO_PARSER_H
#define FLUXIO_PARSER_H

#include "fluxio_token.h"
#include "fluxio_ast.h"

/* Parses a token list into a program AST. Returns NULL and prints a
 * diagnostic to stderr on any syntax or naming/doc-comment convention
 * violation (all such violations are hard compile errors in Fluxio). */
FxProgram* fx_parse(FxTokenList* tokens);

#endif /* FLUXIO_PARSER_H */
