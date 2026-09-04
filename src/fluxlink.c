/* fluxlink -- links a compiled Lux library into a Fluxio app image, per
 * abi/nux-abi.json (docs/quill_fluxio.md Phase B3).
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
 *
 * Output format: see abi/nux-abi.json "linked_image_format". Two design
 * corrections already baked in (both caught by analysis/tests before any
 * of this was written, see abi/nux-abi.json's $comment on that field):
 * the trampoline table is a FIXED size (MM_ABI_TRAMPOLINE_RESERVE) so the
 * library's own code base never moves as exports are added, and the
 * output is one MERGED blob (not two separately-loaded VM images), since
 * the VM only supports a single contiguous execution image.
 */

#include "opcodes.h"
#include "memory_map.h"
#include "rom.h"
#include "sha256.h"
#include "kelvin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_SYMBOLS 4096
#define MAX_EXPORTS ((MM_ABI_TRAMPOLINE_RESERVE - 12) / 5)

typedef struct {
    char name[256];
    int32_t address;
} Symbol;

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
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return NULL; }
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

/* --- Minimal JSON reading, scoped to exactly the two formats fluxlink
 * consumes (both produced by our own tooling): a flat string array for
 * *.exports.json, and { "symbols": [ {"name":..,"address":..}, ... ] }
 * for *.symtab.json. Not a general JSON parser. */

static void skip_ws(const char** p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static bool read_json_string(const char** p, char* out, size_t out_cap) {
    skip_ws(p);
    if (**p != '"') return false;
    (*p)++;
    size_t i = 0;
    while (**p && **p != '"') {
        if (i + 1 < out_cap) out[i++] = **p;
        (*p)++;
    }
    if (**p != '"') return false;
    (*p)++;
    out[i] = '\0';
    return true;
}

/* Parses a flat JSON array of strings, e.g. `["a", "b", "c"]`. */
static int read_exports_array(const char* json, char names[][256], int max_count) {
    const char* p = json;
    skip_ws(&p);
    if (*p != '[') return -1;
    p++;
    int count = 0;
    skip_ws(&p);
    if (*p == ']') return 0;
    while (*p) {
        if (count >= max_count) return -1;
        if (!read_json_string(&p, names[count], 256)) return -1;
        count++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; break; }
        return -1;
    }
    return count;
}

/* Parses `{ "symbols": [ { "name": "...", "address": N }, ... ] }`. */
static int read_symtab(const char* json, Symbol* out, int max_count) {
    const char* p = strstr(json, "\"symbols\"");
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    p++;
    int count = 0;
    skip_ws(&p);
    if (*p == ']') return 0;
    while (*p) {
        skip_ws(&p);
        if (*p != '{') return -1;
        p++;
        char name[256] = {0};
        int32_t address = 0;
        bool have_name = false, have_addr = false;
        while (*p && *p != '}') {
            skip_ws(&p);
            char key[64];
            if (!read_json_string(&p, key, sizeof(key))) return -1;
            skip_ws(&p);
            if (*p != ':') return -1;
            p++;
            skip_ws(&p);
            if (strcmp(key, "name") == 0) {
                if (!read_json_string(&p, name, sizeof(name))) return -1;
                have_name = true;
            } else if (strcmp(key, "address") == 0) {
                char* end;
                address = (int32_t)strtol(p, &end, 10);
                if (end == p) return -1;
                p = end;
                have_addr = true;
            } else {
                return -1;
            }
            skip_ws(&p);
            if (*p == ',') { p++; continue; }
        }
        if (*p != '}') return -1;
        p++;
        if (!have_name || !have_addr) return -1;
        if (count >= max_count) return -1;
        strncpy(out[count].name, name, sizeof(out[count].name) - 1);
        out[count].address = address;
        count++;
        skip_ws(&p);
        if (*p == ',') { p++; continue; }
        if (*p == ']') { p++; break; }
        return -1;
    }
    return count;
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

static int32_t find_symbol(Symbol* symtab, int symtab_count, const char* name) {
    for (int i = 0; i < symtab_count; i++) {
        if (strcmp(symtab[i].name, name) == 0) return symtab[i].address;
    }
    return -1; /* not found; address 0 would be ambiguous with a real (if unlikely) address */
}

int main(int argc, char** argv) {
    const char* lib_path = NULL;
    const char* symtab_path = NULL;
    const char* exports_path = NULL;
    const char* app_path = NULL;
    const char* out_path = NULL;
    const char* prev_exports_path = NULL;
    int32_t app_base = MM_GRAPHICAL_CODE_BASE;
    int32_t lib_base = MM_ABI_LIBRARY_LINK_BASE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lib") == 0 && i + 1 < argc) { lib_path = argv[++i]; }
        else if (strcmp(argv[i], "--symtab") == 0 && i + 1 < argc) { symtab_path = argv[++i]; }
        else if (strcmp(argv[i], "--exports") == 0 && i + 1 < argc) { exports_path = argv[++i]; }
        else if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) { app_path = argv[++i]; }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out_path = argv[++i]; }
        else if (strcmp(argv[i], "--check-append-only") == 0 && i + 1 < argc) { prev_exports_path = argv[++i]; }
        else if (strcmp(argv[i], "--app-base") == 0 && i + 1 < argc) {
            if (!parse_hex_or_dec(argv[++i], &app_base)) { fprintf(stderr, "fluxlink: bad --app-base\n"); return 1; }
        } else if (strcmp(argv[i], "--lib-base") == 0 && i + 1 < argc) {
            if (!parse_hex_or_dec(argv[++i], &lib_base)) { fprintf(stderr, "fluxlink: bad --lib-base\n"); return 1; }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!lib_path || !symtab_path || !exports_path || !app_path || !out_path) {
        usage(argv[0]);
        return 1;
    }

    char* symtab_json = read_file(symtab_path, NULL);
    if (!symtab_json) { fprintf(stderr, "fluxlink: cannot read %s\n", symtab_path); return 1; }
    static Symbol symtab[MAX_SYMBOLS];
    int symtab_count = read_symtab(symtab_json, symtab, MAX_SYMBOLS);
    if (symtab_count < 0) { fprintf(stderr, "fluxlink: cannot parse %s\n", symtab_path); return 1; }

    char* exports_json = read_file(exports_path, NULL);
    if (!exports_json) { fprintf(stderr, "fluxlink: cannot read %s\n", exports_path); return 1; }
    static char exports[MAX_EXPORTS][256];
    int export_count = read_exports_array(exports_json, exports, MAX_EXPORTS);
    if (export_count < 0) {
        fprintf(stderr, "fluxlink: cannot parse %s (or it exceeds the %d-slot trampoline capacity, "
                "see MM_ABI_TRAMPOLINE_RESERVE in include/memory_map.h)\n", exports_path, (int)MAX_EXPORTS);
        return 1;
    }

    /* Append-only enforcement (docs/quill_fluxio.md Phase 0.5 point 2):
     * the committed export list may only grow at the end. */
    if (prev_exports_path) {
        char* prev_json = read_file(prev_exports_path, NULL);
        if (!prev_json) { fprintf(stderr, "fluxlink: cannot read %s\n", prev_exports_path); return 1; }
        static char prev_exports[MAX_EXPORTS][256];
        int prev_count = read_exports_array(prev_json, prev_exports, MAX_EXPORTS);
        if (prev_count < 0) { fprintf(stderr, "fluxlink: cannot parse %s\n", prev_exports_path); return 1; }
        if (prev_count > export_count) {
            fprintf(stderr, "fluxlink: %s has fewer entries (%d) than %s (%d) -- "
                    "removing an export is a breaking ABI change, not allowed\n",
                    exports_path, export_count, prev_exports_path, prev_count);
            return 1;
        }
        for (int i = 0; i < prev_count; i++) {
            if (strcmp(prev_exports[i], exports[i]) != 0) {
                fprintf(stderr, "fluxlink: slot %d changed from \"%s\" (in %s) to \"%s\" (in %s) -- "
                        "existing trampoline slots must never be reordered/renamed, only appended after\n",
                        i, prev_exports[i], prev_exports_path, exports[i], exports_path);
                return 1;
            }
        }
        free(prev_json);
        printf("fluxlink: append-only check OK (%d existing slot(s) unchanged, %d new)\n",
               prev_count, export_count - prev_count);
    }

    /* Resolve each export to an address, and reject duplicate export names. */
    int32_t export_addrs[MAX_EXPORTS];
    for (int i = 0; i < export_count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(exports[i], exports[j]) == 0) {
                fprintf(stderr, "fluxlink: duplicate export \"%s\" (slots %d and %d)\n", exports[i], j, i);
                return 1;
            }
        }
        int32_t addr = find_symbol(symtab, symtab_count, exports[i]);
        if (addr < 0) {
            fprintf(stderr, "fluxlink: exported symbol \"%s\" not found in %s\n", exports[i], symtab_path);
            return 1;
        }
        export_addrs[i] = addr;
    }

    size_t lib_len;
    char* lib_bytes_signed = read_file(lib_path, &lib_len);
    if (!lib_bytes_signed) { fprintf(stderr, "fluxlink: cannot read %s\n", lib_path); return 1; }
    uint8_t* lib_bytes = (uint8_t*)lib_bytes_signed;
    if (rom_has_header(lib_bytes, lib_len)) {
        fprintf(stderr, "fluxlink: %s looks like an app ROM (NUXR header); pass a luxc -base library\n",
                lib_path);
        free(lib_bytes);
        return 1;
    }

    size_t app_file_len;
    char* app_file = read_file(app_path, &app_file_len);
    if (!app_file) { fprintf(stderr, "fluxlink: cannot read %s\n", app_path); free(lib_bytes); return 1; }
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
        free(lib_bytes);
        return 1;
    }
    if (!app_headered) kelvin = CLOISTER_KELVIN;

    int32_t lib_code_base = lib_base + MM_ABI_TRAMPOLINE_RESERVE;
    if (lib_code_base < app_base) {
        fprintf(stderr, "fluxlink: library-link base 0x%X is below the app base 0x%X\n",
                (unsigned)lib_base, (unsigned)app_base);
        return 1;
    }

    size_t total_len = (size_t)(lib_code_base - app_base) + lib_len;
    uint8_t* merged = calloc(1, total_len);
    memcpy(merged, app_bytes, app_len); /* rest of the app..lib_base gap stays zero */

    /* Trampoline header + table at lib_base, fixed MM_ABI_TRAMPOLINE_RESERVE size. */
    uint8_t* tramp = merged + (lib_base - app_base);
    tramp[0] = 'N'; tramp[1] = 'U'; tramp[2] = 'X'; tramp[3] = '1';
    tramp[4] = 0; tramp[5] = 1; /* abi_version_major = 1 */
    tramp[6] = 0; tramp[7] = 0; /* abi_version_minor = 0 */
    tramp[8] = (uint8_t)((export_count >> 24) & 0xFF);
    tramp[9] = (uint8_t)((export_count >> 16) & 0xFF);
    tramp[10] = (uint8_t)((export_count >> 8) & 0xFF);
    tramp[11] = (uint8_t)(export_count & 0xFF);

    size_t off = 12;
    for (int i = 0; i < export_count; i++) {
        tramp[off++] = OP_JMP;
        int32_t addr = export_addrs[i];
        tramp[off++] = (uint8_t)((addr >> 24) & 0xFF);
        tramp[off++] = (uint8_t)((addr >> 16) & 0xFF);
        tramp[off++] = (uint8_t)((addr >> 8) & 0xFF);
        tramp[off++] = (uint8_t)(addr & 0xFF);
    }
    /* Unused slot capacity, up to the fixed reserve: pad with HALT so a
     * stray call to a not-yet-used slot faults cleanly instead of
     * executing whatever garbage followed. */
    while (off < (size_t)MM_ABI_TRAMPOLINE_RESERVE) {
        tramp[off++] = OP_HALT;
    }

    memcpy(merged + (lib_code_base - app_base), lib_bytes, lib_len);

    /* A linked image has no single source file, so its source_sha256 is the
     * digest of everything that went into it: the app's own recorded source
     * digest (zeroes if the app came in raw and never recorded one), plus the
     * library bytes and the two JSON files that decide the trampoline layout.
     * Change any of them and the merged image's provenance moves with it. */
    uint8_t source_sha[ROM_SHA256_LEN];
    {
        Sha256Ctx ctx;
        sha256_init(&ctx);
        uint8_t d[ROM_SHA256_LEN];
        sha256_update(&ctx, app_source_sha, ROM_SHA256_LEN);
        sha256(lib_bytes, lib_len, d);
        sha256_update(&ctx, d, sizeof(d));
        sha256((const uint8_t*)symtab_json, strlen(symtab_json), d);
        sha256_update(&ctx, d, sizeof(d));
        sha256((const uint8_t*)exports_json, strlen(exports_json), d);
        sha256_update(&ctx, d, sizeof(d));
        sha256_final(&ctx, source_sha);
    }

    uint8_t sha[ROM_SHA256_LEN];
    if (!rom_write_file(out_path, merged, total_len, kelvin, source_sha, sha)) {
        fprintf(stderr, "fluxlink: cannot write %s\n", out_path);
        free(symtab_json);
        free(exports_json);
        free(lib_bytes);
        free(app_bytes);
        free(merged);
        return 1;
    }
    char hex[65];
    rom_sha256_hex(sha, hex);

    printf("fluxlink: linked %d export(s) from %s into %s\n", export_count, lib_path, out_path);
    printf("  app:     0x%X (%zu bytes)\n", (unsigned)app_base, app_len);
    printf("  trampoline: 0x%X (%d bytes reserved, %d used)\n",
           (unsigned)lib_base, (int)MM_ABI_TRAMPOLINE_RESERVE, (int)(12 + 5 * export_count));
    printf("  lib code:   0x%X (%zu bytes)\n", (unsigned)lib_code_base, lib_len);
    printf("  merged:     %zu bytes payload, VERSION %d, sha256=%s\n", total_len, kelvin, hex);

    free(symtab_json);
    free(exports_json);
    free(lib_bytes);
    free(app_bytes);
    free(merged);
    return 0;
}
