/* fluxlink core -- the actual link, shared by bin/fluxlink and cloister's
 * on-the-fly `.fx` launch path. See include/fluxlink.h for the contract and
 * abi/nux-abi.json "linked_image_format" for the output layout.
 *
 * Two design corrections are baked into that format (both caught by analysis
 * and tests before any of it was written, see abi/nux-abi.json's $comment on
 * that field): the trampoline table is a FIXED size
 * (MM_ABI_TRAMPOLINE_RESERVE) so the library's own code base never moves as
 * exports are added, and the output is one MERGED blob (not two separately
 * loaded VM images), since the VM only supports a single contiguous
 * execution image. */

#include "fluxlink.h"
#include "opcodes.h"
#include "memory_map.h"
#include "rom.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_SYMBOLS 4096
#define MAX_EXPORTS ((MM_ABI_TRAMPOLINE_RESERVE - 12) / 5)

typedef struct {
    char name[256];
    int32_t address;
} Symbol;

static void set_err(char* err, size_t err_cap, const char* fmt, ...) {
    if (!err || err_cap == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_cap, fmt, ap);
    va_end(ap);
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

static int32_t find_symbol(Symbol* symtab, int symtab_count, const char* name) {
    for (int i = 0; i < symtab_count; i++) {
        if (strcmp(symtab[i].name, name) == 0) return symtab[i].address;
    }
    return -1; /* not found; address 0 would be ambiguous with a real (if unlikely) address */
}

void fluxlink_spec_defaults(FluxlinkSpec* spec) {
    spec->lib_path = FLUXLINK_UISF_LIB;
    spec->symtab_path = FLUXLINK_UISF_SYMTAB;
    spec->exports_path = FLUXLINK_UISF_EXPORTS;
    spec->app_base = MM_GRAPHICAL_CODE_BASE;
    spec->lib_base = MM_ABI_LIBRARY_LINK_BASE;
}

bool fluxlink_check_append_only(const char* exports_path, const char* prev_exports_path,
                                int* out_prev_count, int* out_count,
                                char* err, size_t err_cap) {
    static char exports[MAX_EXPORTS][256];
    static char prev_exports[MAX_EXPORTS][256];

    char* json = read_file(exports_path, NULL);
    if (!json) { set_err(err, err_cap, "cannot read %s", exports_path); return false; }
    int count = read_exports_array(json, exports, MAX_EXPORTS);
    free(json);
    if (count < 0) { set_err(err, err_cap, "cannot parse %s", exports_path); return false; }

    char* prev_json = read_file(prev_exports_path, NULL);
    if (!prev_json) { set_err(err, err_cap, "cannot read %s", prev_exports_path); return false; }
    int prev_count = read_exports_array(prev_json, prev_exports, MAX_EXPORTS);
    free(prev_json);
    if (prev_count < 0) { set_err(err, err_cap, "cannot parse %s", prev_exports_path); return false; }

    if (prev_count > count) {
        set_err(err, err_cap,
                "%s has fewer entries (%d) than %s (%d) -- removing an export is a "
                "breaking ABI change, not allowed",
                exports_path, count, prev_exports_path, prev_count);
        return false;
    }
    for (int i = 0; i < prev_count; i++) {
        if (strcmp(prev_exports[i], exports[i]) != 0) {
            set_err(err, err_cap,
                    "slot %d changed from \"%s\" (in %s) to \"%s\" (in %s) -- existing "
                    "trampoline slots must never be reordered/renamed, only appended after",
                    i, prev_exports[i], prev_exports_path, exports[i], exports_path);
            return false;
        }
    }
    if (out_prev_count) *out_prev_count = prev_count;
    if (out_count) *out_count = count;
    return true;
}

uint8_t* fluxlink_merge(const FluxlinkSpec* spec,
                        const uint8_t* app_bytes, size_t app_len,
                        const uint8_t* app_source_sha,
                        size_t* out_len,
                        uint8_t out_source_sha[ROM_SHA256_LEN],
                        int* out_export_count,
                        char* err, size_t err_cap) {
    static Symbol symtab[MAX_SYMBOLS];
    static char exports[MAX_EXPORTS][256];
    static int32_t export_addrs[MAX_EXPORTS];

    char* symtab_json = read_file(spec->symtab_path, NULL);
    if (!symtab_json) { set_err(err, err_cap, "cannot read %s", spec->symtab_path); return NULL; }
    int symtab_count = read_symtab(symtab_json, symtab, MAX_SYMBOLS);
    if (symtab_count < 0) {
        set_err(err, err_cap, "cannot parse %s", spec->symtab_path);
        free(symtab_json);
        return NULL;
    }

    char* exports_json = read_file(spec->exports_path, NULL);
    if (!exports_json) {
        set_err(err, err_cap, "cannot read %s", spec->exports_path);
        free(symtab_json);
        return NULL;
    }
    int export_count = read_exports_array(exports_json, exports, MAX_EXPORTS);
    if (export_count < 0) {
        set_err(err, err_cap,
                "cannot parse %s (or it exceeds the %d-slot trampoline capacity, see "
                "MM_ABI_TRAMPOLINE_RESERVE in include/memory_map.h)",
                spec->exports_path, (int)MAX_EXPORTS);
        free(symtab_json);
        free(exports_json);
        return NULL;
    }

    /* Resolve each export to an address, and reject duplicate export names. */
    for (int i = 0; i < export_count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(exports[i], exports[j]) == 0) {
                set_err(err, err_cap, "duplicate export \"%s\" (slots %d and %d)", exports[i], j, i);
                free(symtab_json);
                free(exports_json);
                return NULL;
            }
        }
        int32_t addr = find_symbol(symtab, symtab_count, exports[i]);
        if (addr < 0) {
            set_err(err, err_cap, "exported symbol \"%s\" not found in %s",
                    exports[i], spec->symtab_path);
            free(symtab_json);
            free(exports_json);
            return NULL;
        }
        export_addrs[i] = addr;
    }

    size_t lib_len = 0;
    char* lib_bytes_signed = read_file(spec->lib_path, &lib_len);
    if (!lib_bytes_signed) {
        set_err(err, err_cap, "cannot read %s", spec->lib_path);
        free(symtab_json);
        free(exports_json);
        return NULL;
    }
    uint8_t* lib_bytes = (uint8_t*)lib_bytes_signed;
    if (rom_has_header(lib_bytes, lib_len)) {
        set_err(err, err_cap, "%s looks like an app ROM (NUXR header); pass a luxc -base library",
                spec->lib_path);
        free(symtab_json);
        free(exports_json);
        free(lib_bytes);
        return NULL;
    }

    int32_t lib_code_base = spec->lib_base + MM_ABI_TRAMPOLINE_RESERVE;
    if (lib_code_base < spec->app_base) {
        set_err(err, err_cap, "library-link base 0x%X is below the app base 0x%X",
                (unsigned)spec->lib_base, (unsigned)spec->app_base);
        free(symtab_json);
        free(exports_json);
        free(lib_bytes);
        return NULL;
    }
    if (app_len > (size_t)(spec->lib_base - spec->app_base)) {
        set_err(err, err_cap,
                "app image is %zu bytes and overruns the trampoline table at 0x%X "
                "(only %zu bytes available from 0x%X)",
                app_len, (unsigned)spec->lib_base,
                (size_t)(spec->lib_base - spec->app_base), (unsigned)spec->app_base);
        free(symtab_json);
        free(exports_json);
        free(lib_bytes);
        return NULL;
    }

    size_t total_len = (size_t)(lib_code_base - spec->app_base) + lib_len;
    uint8_t* merged = calloc(1, total_len);
    if (!merged) {
        set_err(err, err_cap, "out of memory merging %zu bytes", total_len);
        free(symtab_json);
        free(exports_json);
        free(lib_bytes);
        return NULL;
    }
    memcpy(merged, app_bytes, app_len); /* rest of the app..lib_base gap stays zero */

    /* Trampoline header + table at lib_base, fixed MM_ABI_TRAMPOLINE_RESERVE size. */
    uint8_t* tramp = merged + (spec->lib_base - spec->app_base);
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

    memcpy(merged + (lib_code_base - spec->app_base), lib_bytes, lib_len);

    /* A linked image has no single source file, so its source_sha256 is the
     * digest of everything that went into it: the app's own recorded source
     * digest (zeroes if the app came in raw and never recorded one), plus the
     * library bytes and the two JSON files that decide the trampoline layout.
     * Change any of them and the merged image's provenance moves with it. */
    if (out_source_sha) {
        static const uint8_t zero_sha[ROM_SHA256_LEN] = {0};
        Sha256Ctx ctx;
        sha256_init(&ctx);
        uint8_t d[ROM_SHA256_LEN];
        sha256_update(&ctx, app_source_sha ? app_source_sha : zero_sha, ROM_SHA256_LEN);
        sha256(lib_bytes, lib_len, d);
        sha256_update(&ctx, d, sizeof(d));
        sha256((const uint8_t*)symtab_json, strlen(symtab_json), d);
        sha256_update(&ctx, d, sizeof(d));
        sha256((const uint8_t*)exports_json, strlen(exports_json), d);
        sha256_update(&ctx, d, sizeof(d));
        sha256_final(&ctx, out_source_sha);
    }

    free(symtab_json);
    free(exports_json);
    free(lib_bytes);

    if (out_len) *out_len = total_len;
    if (out_export_count) *out_export_count = export_count;
    return merged;
}
