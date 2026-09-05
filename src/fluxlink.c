/* fluxlink -- links a compiled Lux library into a Fluxio app image, per
 * abi/nux-abi.json (docs/quill_fluxio.md Phase B3).
 *
 * This file is the CLI driver only; the link itself lives in
 * src/fluxlink_core.c behind include/fluxlink.h, so cloister can run the
 * same merge in-process when launching a `.fx` app from source.
 *
 * Inputs:
 *   --lib     <lib.bin>       compiled via `luxc -base 0x<MM_ABI_LIBRARY_CODE_BASE>`
 *   --symtab  <lib.symtab.json>  full dictionary dump, from `luxc -symbols`
 *   --exports <lib.exports.json> ordered array of names to expose as trampoline
 *             slots -- this is the COMMITTED file (docs/quill_fluxio.md Phase
 *             0.5 point 2): once checked in, existing entries may never be
 *             reordered/renamed/removed, only appended to.
 *   --app     <app.bin>       compiled Fluxio (or Lux) program to link against
 *   --app-base 0xADDR         where app.bin loads (default MM_GRAPHICAL_CODE_BASE)
 *   --lib-base 0xADDR         trampoline table base (default MM_ABI_LIBRARY_LINK_BASE)
 *   --check-append-only <prev.exports.json>  verify --exports only appends
 *             relative to this previously-committed file
 *   -o <out.bin>              merged image to write
 */

#include "fluxlink.h"
#include "memory_map.h"
#include "rom.h"
#include "kelvin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s --lib lib.bin --symtab lib.symtab.json --exports lib.exports.json\n"
        "       %*s --app app.bin [--app-base 0xADDR] [--lib-base 0xADDR]\n"
        "       %*s [--check-append-only prev.exports.json] -o out.bin\n",
        prog, (int)strlen(prog), "", (int)strlen(prog), "");
}

static char* read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static bool parse_hex_or_dec(const char* s, int32_t* out) {
    char* end;
    long v;
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        v = strtol(s + 2, &end, 16);
    } else {
        v = strtol(s, &end, 10);
    }
    if (end == s || *end != '\0') return false;
    *out = (int32_t)v;
    return true;
}

int main(int argc, char** argv) {
    FluxlinkSpec spec;
    fluxlink_spec_defaults(&spec);
    spec.lib_path = NULL;
    spec.symtab_path = NULL;
    spec.exports_path = NULL;

    const char* app_path = NULL;
    const char* out_path = NULL;
    const char* prev_exports_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lib") == 0 && i + 1 < argc) { spec.lib_path = argv[++i]; }
        else if (strcmp(argv[i], "--symtab") == 0 && i + 1 < argc) { spec.symtab_path = argv[++i]; }
        else if (strcmp(argv[i], "--exports") == 0 && i + 1 < argc) { spec.exports_path = argv[++i]; }
        else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) { app_path = argv[++i]; }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out_path = argv[++i]; }
        else if (strcmp(argv[i], "--check-append-only") == 0 && i + 1 < argc) { prev_exports_path = argv[++i]; }
        else if (strcmp(argv[i], "--app-base") == 0 && i + 1 < argc) {
            if (!parse_hex_or_dec(argv[++i], &spec.app_base)) { fprintf(stderr, "fluxlink: bad --app-base\n"); return 1; }
        } else if (strcmp(argv[i], "--lib-base") == 0 && i + 1 < argc) {
            if (!parse_hex_or_dec(argv[++i], &spec.lib_base)) { fprintf(stderr, "fluxlink: bad --lib-base\n"); return 1; }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!spec.lib_path || !spec.symtab_path || !spec.exports_path || !app_path || !out_path) {
        usage(argv[0]);
        return 1;
    }

    char err[512];

    /* Append-only enforcement (docs/quill_fluxio.md Phase 0.5 point 2):
     * the committed export list may only grow at the end. Build-time only --
     * cloister's on-the-fly link deliberately does not repeat this. */
    if (prev_exports_path) {
        int prev_count = 0, count = 0;
        if (!fluxlink_check_append_only(spec.exports_path, prev_exports_path,
                                        &prev_count, &count, err, sizeof(err))) {
            fprintf(stderr, "fluxlink: %s\n", err);
            return 1;
        }
        printf("fluxlink: append-only check OK (%d existing slot(s) unchanged, %d new)\n",
               prev_count, count - prev_count);
    }

    size_t app_file_len;
    char* app_file = read_file(app_path, &app_file_len);
    if (!app_file) { fprintf(stderr, "fluxlink: cannot read %s\n", app_path); return 1; }
    char romerr[256];
    size_t app_len = 0;
    int32_t kelvin = 0;
    bool app_headered = false;
    uint8_t app_source_sha[ROM_SHA256_LEN];
    uint8_t* app_bytes = rom_open_image((uint8_t*)app_file, app_file_len, &app_len, &kelvin,
                                        &app_headered, app_source_sha, romerr, sizeof(romerr));
    free(app_file);
    if (!app_bytes) {
        fprintf(stderr, "fluxlink: %s: %s\n", app_path, romerr);
        return 1;
    }
    if (!app_headered) kelvin = CLOISTER_KELVIN;

    size_t total_len = 0;
    int export_count = 0;
    uint8_t source_sha[ROM_SHA256_LEN];
    uint8_t* merged = fluxlink_merge(&spec, app_bytes, app_len, app_source_sha,
                                     &total_len, source_sha, &export_count,
                                     err, sizeof(err));
    free(app_bytes);
    if (!merged) {
        fprintf(stderr, "fluxlink: %s\n", err);
        return 1;
    }

    uint8_t sha[ROM_SHA256_LEN];
    if (!rom_write_file(out_path, merged, total_len, kelvin, source_sha, sha)) {
        fprintf(stderr, "fluxlink: cannot write %s\n", out_path);
        free(merged);
        return 1;
    }
    char hex[65];
    rom_sha256_hex(sha, hex);

    int32_t lib_code_base = spec.lib_base + MM_ABI_TRAMPOLINE_RESERVE;
    printf("fluxlink: linked %d export(s) from %s into %s\n", export_count, spec.lib_path, out_path);
    printf("  app:     0x%X (%zu bytes)\n", (unsigned)spec.app_base, app_len);
    printf("  trampoline: 0x%X (%d bytes reserved, %d used)\n",
           (unsigned)spec.lib_base, (int)MM_ABI_TRAMPOLINE_RESERVE, (int)(12 + 5 * export_count));
    printf("  lib code:   0x%X (%zu bytes)\n",
           (unsigned)lib_code_base, total_len - (size_t)(lib_code_base - spec.app_base));
    printf("  merged:     %zu bytes payload, VERSION %d, sha256=%s\n", total_len, kelvin, hex);

    free(merged);
    return 0;
}
