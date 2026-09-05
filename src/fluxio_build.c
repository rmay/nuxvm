#include "fluxio_build.h"
#include "fluxio_ast.h"
#include "fluxio_include.h"
#include "fluxio_parser.h"
#include "fluxio_codegen.h"
#include "fluxlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

uint8_t* fx_build_image(const char* path, int32_t base_addr, const char* tool, size_t* out_len) {
    FxTokenList* tokens = fx_load_with_includes(path);
    if (!tokens) {
        fprintf(stderr, "%s: %s: lexing failed\n", tool, path);
        return NULL;
    }
    FxProgram* program = fx_parse(tokens);
    fx_token_list_free(tokens);
    if (!program) {
        fprintf(stderr, "%s: %s: parsing failed\n", tool, path);
        return NULL;
    }
    if (!fx_require_version(program, tool, path)) {
        fx_program_free(program);
        return NULL;
    }

    /* An app that declares `extern`s binds them to fixed addresses in the
     * trampoline table (see apps/fluxio/Quill.fx), which only exists once the
     * Lux UI/SF library has been merged in. So externs are the signal that
     * this image needs the same link the Makefile's Quill.bin rule performs;
     * an app with none (Snake.fx, HelloCloister.fx) runs straight out of
     * codegen. */
    bool needs_link = program->nexterns > 0;
    size_t code_len = 0;
    uint8_t* code = fx_codegen(program, base_addr, &code_len);
    fx_program_free(program);
    if (!code) {
        fprintf(stderr, "%s: %s: compilation failed\n", tool, path);
        return NULL;
    }
    if (!needs_link) {
        *out_len = code_len;
        return code;
    }

    /* The app was built here and has no recorded source digest, so pass NULL
     * and skip the merged image's provenance digest too: nothing is written
     * out, the blob goes straight into a Machine. */
    FluxlinkSpec spec;
    fluxlink_spec_defaults(&spec);
    spec.app_base = base_addr;
    char err[512];
    size_t merged_len = 0;
    uint8_t* merged = fluxlink_merge(&spec, code, code_len, NULL,
                                     &merged_len, NULL, NULL, err, sizeof(err));
    free(code);
    if (!merged) {
        fprintf(stderr, "%s: %s: %s\n", tool, path, err);
        fprintf(stderr, "%s: %s declares externs, so it needs the Lux UI/SF library linked in "
                        "-- run `make uilib` to build %s.\n", tool, path, FLUXLINK_UISF_LIB);
        return NULL;
    }
    *out_len = merged_len;
    return merged;
}
