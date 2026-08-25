#ifndef FLUXIO_CODEGEN_H
#define FLUXIO_CODEGEN_H

#include "fluxio_ast.h"
#include <stdint.h>
#include <stddef.h>

/* Low RAM base for the globals segment. Always below device/MMIO space
 * (0x10000) and outside every program image, so writes never fault. */
#define FX_GLOBALS_BASE 0x1000

/* Compiles a parsed program into a raw NUX bytecode blob loaded at
 * base_addr. Returns NULL and prints a diagnostic to stderr on any
 * semantic error (undefined name, arity mismatch, unbounded recursion
 * cycle, missing/malformed main, globals-region overflow). Caller must
 * free() the returned buffer. */
uint8_t* fx_codegen(FxProgram* program, int32_t base_addr, size_t* out_len);

#endif /* FLUXIO_CODEGEN_H */
